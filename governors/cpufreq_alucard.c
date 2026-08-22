/*
 * drivers/cpufreq/cpufreq_alucard.c
 *
 * 'alucard' - tunable step-based governor with separate up/down steps.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct alucard_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
};

static inline struct alucard_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct alucard_policy_dbs_info, policy_dbs);
}

struct alucard_dbs_tuners {
	unsigned int down_threshold;
	unsigned int up_step_pct;
	unsigned int down_step_pct;
};

#define ALUCARD_DEF_UP_THRESHOLD	(80)
#define ALUCARD_DEF_DOWN_THRESHOLD	(35)
#define ALUCARD_DEF_UP_STEP_PCT		(25)
#define ALUCARD_DEF_DOWN_STEP_PCT	(5)

static unsigned int alucard_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct alucard_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct alucard_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);

	if (requested_freq > policy->max || requested_freq < policy->min)
		requested_freq = policy->cur;

	if (load > dbs_data->up_threshold) {
		requested_freq += (tuners->up_step_pct * policy->max) / 100;
		if (requested_freq > policy->max)
			requested_freq = policy->max;

		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
	} else if (load < tuners->down_threshold) {
		unsigned int step = (tuners->down_step_pct * policy->max) / 100;

		if (step == 0)
			step = policy->max / 100;

		if (requested_freq > step)
			requested_freq -= step;
		else
			requested_freq = policy->min;

		if (requested_freq < policy->min)
			requested_freq = policy->min;

		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_L);
		dbs_info->requested_freq = requested_freq;
	}

	return dbs_data->sampling_rate;
}

/************************** sysfs interface ************************/

static ssize_t store_up_threshold(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct alucard_dbs_tuners *tuners = dbs_data->tuners;
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
	struct alucard_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input < 1 || input > 100 ||
	    input >= dbs_data->up_threshold)
		return -EINVAL;

	tuners->down_threshold = input;
	return count;
}

static ssize_t store_up_step_pct(struct gov_attr_set *attr_set, const char *buf,
				 size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct alucard_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->up_step_pct = input;
	return count;
}

static ssize_t store_down_step_pct(struct gov_attr_set *attr_set,
				   const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct alucard_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100)
		return -EINVAL;

	tuners->down_step_pct = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(min_sampling_rate);
gov_show_one(alucard, down_threshold);
gov_show_one(alucard, up_step_pct);
gov_show_one(alucard, down_step_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold);
gov_attr_rw(up_step_pct);
gov_attr_rw(down_step_pct);

static struct attribute *alucard_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&up_step_pct.attr,
	&down_step_pct.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *alucard_alloc(void)
{
	struct alucard_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void alucard_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int alucard_init(struct dbs_data *dbs_data)
{
	struct alucard_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->down_threshold = ALUCARD_DEF_DOWN_THRESHOLD;
	tuners->up_step_pct = ALUCARD_DEF_UP_STEP_PCT;
	tuners->down_step_pct = ALUCARD_DEF_DOWN_STEP_PCT;
	dbs_data->up_threshold = ALUCARD_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void alucard_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void alucard_start(struct cpufreq_policy *policy)
{
	struct alucard_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
}

struct dbs_governor alucard_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("alucard"),
	.kobj_type = { .default_attrs = alucard_attributes },
	.gov_dbs_timer = alucard_dbs_timer,
	.alloc = alucard_alloc,
	.free = alucard_free,
	.init = alucard_init,
	.exit = alucard_exit,
	.start = alucard_start,
};

#define CPU_FREQ_GOV_ALUCARD	(&alucard_governor.gov)

static int __init cpufreq_gov_alucard_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_ALUCARD);
}

static void __exit cpufreq_gov_alucard_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_ALUCARD);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_alucard' - dual-step tunable governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_alucard_init);
module_exit(cpufreq_gov_alucard_exit);
