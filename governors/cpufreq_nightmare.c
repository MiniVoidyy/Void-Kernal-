/*
 * drivers/cpufreq/cpufreq_nightmare.c
 *
 * 'nightmare' - two-phase aggressive governor: instant max on heavy load,
 * stepped boost on medium load, halving decay below low threshold.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct nightmare_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
};

static inline struct nightmare_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct nightmare_policy_dbs_info, policy_dbs);
}

struct nightmare_dbs_tuners {
	unsigned int mid_threshold;
	unsigned int low_threshold;
	unsigned int up_step_pct;
};

#define NIGHTMARE_DEF_UP_THRESHOLD	(75)
#define NIGHTMARE_DEF_MID_THRESHOLD	(60)
#define NIGHTMARE_DEF_LOW_THRESHOLD	(30)
#define NIGHTMARE_DEF_UP_STEP_PCT	(50)

static unsigned int nightmare_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct nightmare_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct nightmare_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);

	if (requested_freq > policy->max || requested_freq < policy->min)
		requested_freq = policy->cur;

	if (load > dbs_data->up_threshold) {
		if (load > 90)
			requested_freq = policy->max;
		else
			requested_freq += (tuners->up_step_pct * policy->max) / 100;

		if (requested_freq > policy->max)
			requested_freq = policy->max;

		__cpufreq_driver_target(policy, requested_freq, CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
		goto out;
	}

	if (load < tuners->low_threshold) {
		unsigned int step = policy->max / 10;

		if (requested_freq == policy->min)
			goto out;

		if (requested_freq > step)
			requested_freq -= step;
		else
			requested_freq = policy->min;

		__cpufreq_driver_target(policy, requested_freq, CPUFREQ_RELATION_L);
		dbs_info->requested_freq = requested_freq;
	} else if (load < tuners->mid_threshold && requested_freq < policy->max) {
		requested_freq += policy->max / 20;
		if (requested_freq > policy->max)
			requested_freq = policy->max;
		__cpufreq_driver_target(policy, requested_freq, CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
	}

out:
	return dbs_data->sampling_rate;
}

/************************** sysfs interface ************************/

static ssize_t store_up_threshold(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct nightmare_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input <= tuners->mid_threshold)
		return -EINVAL;

	dbs_data->up_threshold = input;
	return count;
}

static ssize_t store_mid_threshold(struct gov_attr_set *attr_set,
				   const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct nightmare_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input >= dbs_data->up_threshold ||
	    input <= tuners->low_threshold)
		return -EINVAL;

	tuners->mid_threshold = input;
	return count;
}

static ssize_t store_low_threshold(struct gov_attr_set *attr_set,
				   const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct nightmare_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input >= tuners->mid_threshold || input < 1)
		return -EINVAL;

	tuners->low_threshold = input;
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

static ssize_t store_up_step_pct(struct gov_attr_set *attr_set, const char *buf,
				 size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct nightmare_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->up_step_pct = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(ignore_nice_load);
gov_show_one_common(min_sampling_rate);
gov_show_one(nightmare, mid_threshold);
gov_show_one(nightmare, low_threshold);
gov_show_one(nightmare, up_step_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_rw(ignore_nice_load);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(mid_threshold);
gov_attr_rw(low_threshold);
gov_attr_rw(up_step_pct);

static struct attribute *nightmare_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&mid_threshold.attr,
	&low_threshold.attr,
	&up_step_pct.attr,
	&ignore_nice_load.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *nightmare_alloc(void)
{
	struct nightmare_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void nightmare_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int nightmare_init(struct dbs_data *dbs_data)
{
	struct nightmare_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->mid_threshold = NIGHTMARE_DEF_MID_THRESHOLD;
	tuners->low_threshold = NIGHTMARE_DEF_LOW_THRESHOLD;
	tuners->up_step_pct = NIGHTMARE_DEF_UP_STEP_PCT;
	dbs_data->up_threshold = NIGHTMARE_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void nightmare_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void nightmare_start(struct cpufreq_policy *policy)
{
	struct nightmare_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
}

struct dbs_governor nightmare_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("nightmare"),
	.kobj_type = { .default_attrs = nightmare_attributes },
	.gov_dbs_timer = nightmare_dbs_timer,
	.alloc = nightmare_alloc,
	.free = nightmare_free,
	.init = nightmare_init,
	.exit = nightmare_exit,
	.start = nightmare_start,
};

#define CPU_FREQ_GOV_NIGHTMARE	(&nightmare_governor.gov)

static int __init cpufreq_gov_nightmare_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_NIGHTMARE);
}

static void __exit cpufreq_gov_nightmare_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_NIGHTMARE);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_nightmare' - two-phase aggressive governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_nightmare_init);
module_exit(cpufreq_gov_nightmare_exit);
