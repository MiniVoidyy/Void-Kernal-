/*
 * drivers/cpufreq/cpufreq_intelliactive.c
 *
 * 'intelliactive' - interactive-style behaviour in the generic dbs
 * framework: hispeed jump on breach, straight decay toward min when idle.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct intelliactive_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int target_freq;
};

static inline struct intelliactive_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct intelliactive_policy_dbs_info, policy_dbs);
}

struct intelliactive_dbs_tuners {
	unsigned int hispeed_pct;
	unsigned int idle_drop_pct;
};

#define IA_DEF_UP_THRESHOLD	(70)
#define IA_DEF_HISPEED_PCT	(80)
#define IA_DEF_IDLE_DROP_PCT	(15)

static unsigned int intelliactive_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct intelliactive_policy_dbs_info *dbs_info =
		to_dbs_info(policy_dbs);
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct intelliactive_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	unsigned int hispeed_freq =
		(tuners->hispeed_pct * policy->max) / 100;

	if (load > dbs_data->up_threshold) {
		if (dbs_info->target_freq >= hispeed_freq)
			dbs_info->target_freq = policy->max;
		else
			dbs_info->target_freq = hispeed_freq;

		__cpufreq_driver_target(policy, dbs_info->target_freq,
					CPUFREQ_RELATION_H);
		goto out;
	}

	if (load < 10) {
		dbs_info->target_freq -=
			(tuners->idle_drop_pct * policy->max) / 100;
		if ((int)dbs_info->target_freq < policy->min)
			dbs_info->target_freq = policy->min;

		__cpufreq_driver_target(policy, dbs_info->target_freq,
					CPUFREQ_RELATION_L);
	} else if (load < dbs_data->up_threshold &&
		   dbs_info->target_freq > hispeed_freq) {
		dbs_info->target_freq = hispeed_freq;
		__cpufreq_driver_target(policy, hispeed_freq,
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
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 11)
		return -EINVAL;

	dbs_data->up_threshold = input;
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

static ssize_t store_hispeed_pct(struct gov_attr_set *attr_set,
				 const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct intelliactive_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->hispeed_pct = input;
	return count;
}

static ssize_t store_idle_drop_pct(struct gov_attr_set *attr_set,
				   const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct intelliactive_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->idle_drop_pct = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(ignore_nice_load);
gov_show_one_common(min_sampling_rate);
gov_show_one(intelliactive, hispeed_pct);
gov_show_one(intelliactive, idle_drop_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_rw(ignore_nice_load);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(hispeed_pct);
gov_attr_rw(idle_drop_pct);

static struct attribute *intelliactive_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&hispeed_pct.attr,
	&idle_drop_pct.attr,
	&ignore_nice_load.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *intelliactive_alloc(void)
{
	struct intelliactive_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void intelliactive_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int intelliactive_init(struct dbs_data *dbs_data)
{
	struct intelliactive_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->hispeed_pct = IA_DEF_HISPEED_PCT;
	tuners->idle_drop_pct = IA_DEF_IDLE_DROP_PCT;
	dbs_data->up_threshold = IA_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void intelliactive_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void intelliactive_start(struct cpufreq_policy *policy)
{
	struct intelliactive_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->target_freq = policy->cur;
}

struct dbs_governor intelliactive_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("intelliactive"),
	.kobj_type = { .default_attrs = intelliactive_attributes },
	.gov_dbs_timer = intelliactive_dbs_timer,
	.alloc = intelliactive_alloc,
	.free = intelliactive_free,
	.init = intelliactive_init,
	.exit = intelliactive_exit,
	.start = intelliactive_start,
};

#define CPU_FREQ_GOV_INTELLIACTIVE	(&intelliactive_governor.gov)

static int __init cpufreq_gov_intelliactive_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_INTELLIACTIVE);
}

static void __exit cpufreq_gov_intelliactive_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_INTELLIACTIVE);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_intelliactive' - interactive-style dbs governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_intelliactive_init);
module_exit(cpufreq_gov_intelliactive_exit);
