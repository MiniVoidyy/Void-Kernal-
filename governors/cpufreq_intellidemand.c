/*
 * drivers/cpufreq/cpufreq_intellidemand.c
 *
 * 'intellidemand' - ondemand-style governor with iowait awareness gating:
 * treats storage I/O waits as busy time only when enabled at runtime.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct intellidemand_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
};

static inline struct intellidemand_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct intellidemand_policy_dbs_info, policy_dbs);
}

struct intellidemand_dbs_tuners {
	unsigned int down_threshold;
	unsigned int freq_step;
};

#define ID_DEF_UP_THRESHOLD	(80)
#define ID_DEF_DOWN_THRESHOLD	(40)
#define ID_DEF_FREQ_STEP	(20)

static unsigned int intellidemand_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct intellidemand_policy_dbs_info *dbs_info =
		to_dbs_info(policy_dbs);
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct intellidemand_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	unsigned int requested_freq = policy->cur;

	if (load > dbs_data->up_threshold) {
		requested_freq = policy->max;
		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_H);
		goto out;
	}

	if (load < tuners->down_threshold) {
		unsigned int freq_target =
			(tuners->freq_step * policy->max) / 100;

		if (freq_target == 0)
			freq_target = ID_DEF_FREQ_STEP;

		if (requested_freq > freq_target)
			requested_freq -= freq_target;
		else
			requested_freq = policy->min;

		if (requested_freq < policy->min)
			requested_freq = policy->min;

		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_L);
	}

out:
	return dbs_data->sampling_rate;
}

/************************** sysfs interface ************************/

static ssize_t store_up_threshold(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct intellidemand_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input <= tuners->down_threshold)
		return -EINVAL;

	dbs_data->up_threshold = input;
	return count;
}

static ssize_t store_down_threshold(struct gov_attr_set *attr_set,
				    const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct intellidemand_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input < 1 || input > 100 ||
	    input >= dbs_data->up_threshold)
		return -EINVAL;

	tuners->down_threshold = input;
	return count;
}

static ssize_t store_ignore_nice_load(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == dbs_data->ignore_nice_load)
		return count;

	dbs_data->ignore_nice_load = input;
	gov_update_cpu_data(dbs_data);

	return count;
}

static ssize_t store_io_is_busy(struct gov_attr_set *attr_set,
				const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == dbs_data->io_is_busy)
		return count;

	dbs_data->io_is_busy = !!input;
	gov_update_cpu_data(dbs_data);

	return count;
}

static ssize_t store_freq_step(struct gov_attr_set *attr_set, const char *buf,
			       size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct intellidemand_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 100)
		input = 100;

	tuners->freq_step = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(ignore_nice_load);
gov_show_one_common(io_is_busy);
gov_show_one_common(min_sampling_rate);
gov_show_one(intellidemand, down_threshold);
gov_show_one(intellidemand, freq_step);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_rw(ignore_nice_load);
gov_attr_rw(io_is_busy);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold);
gov_attr_rw(freq_step);

static struct attribute *intellidemand_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&freq_step.attr,
	&ignore_nice_load.attr,
	&io_is_busy.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *intellidemand_alloc(void)
{
	struct intellidemand_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void intellidemand_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int intellidemand_init(struct dbs_data *dbs_data)
{
	struct intellidemand_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->down_threshold = ID_DEF_DOWN_THRESHOLD;
	tuners->freq_step = ID_DEF_FREQ_STEP;
	dbs_data->up_threshold = ID_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->io_is_busy = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void intellidemand_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

struct dbs_governor intellidemand_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("intellidemand"),
	.kobj_type = { .default_attrs = intellidemand_attributes },
	.gov_dbs_timer = intellidemand_dbs_timer,
	.alloc = intellidemand_alloc,
	.free = intellidemand_free,
	.init = intellidemand_init,
	.exit = intellidemand_exit,
};

#define CPU_FREQ_GOV_INTELLIDEMAND	(&intellidemand_governor.gov)

static int __init cpufreq_gov_intellidemand_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_INTELLIDEMAND);
}

static void __exit cpufreq_gov_intellidemand_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_INTELLIDEMAND);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_intellidemand' - iowait aware demand governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_intellidemand_init);
module_exit(cpufreq_gov_intellidemand_exit);
