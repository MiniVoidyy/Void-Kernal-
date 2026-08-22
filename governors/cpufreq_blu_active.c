/*
 * drivers/cpufreq/cpufreq_blu_active.c
 *
 * 'blu_active' - hispeed-first governor: first breach jumps to hispeed
 * frequency, sustained load climbs to max; smooth decay when idle.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct blu_active_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
	bool hispeed_active;
};

static inline struct blu_active_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct blu_active_policy_dbs_info, policy_dbs);
}

struct blu_active_dbs_tuners {
	unsigned int hispeed_pct;
	unsigned int down_threshold;
	unsigned int ramp_step;
};

#define BLU_DEF_UP_THRESHOLD	(85)
#define BLU_DEF_DOWN_THRESHOLD	(40)
#define BLU_DEF_HISPEED_PCT	(78)
#define BLU_DEF_RAMP_STEP	(20)

static unsigned int blu_active_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct blu_active_policy_dbs_info *dbs_info =
		to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct blu_active_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	unsigned int hispeed_freq =
		(tuners->hispeed_pct * policy->max) / 100;

	if (requested_freq > policy->max || requested_freq < policy->min) {
		requested_freq = policy->cur;
		dbs_info->hispeed_active = false;
	}

	if (load > dbs_data->up_threshold) {
		if (!dbs_info->hispeed_active && requested_freq < hispeed_freq) {
			requested_freq = hispeed_freq;
			dbs_info->hispeed_active = true;
		} else {
			requested_freq += (tuners->ramp_step * policy->max) / 100;
			if (requested_freq > policy->max)
				requested_freq = policy->max;
			if (requested_freq == policy->max)
				dbs_info->hispeed_active = false;
		}
		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
		goto out;
	}

	if (load < tuners->down_threshold) {
		dbs_info->hispeed_active = false;
		requested_freq -= (tuners->ramp_step * policy->max) / 200;
		if ((int)requested_freq < policy->min)
			requested_freq = policy->min;

		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_L);
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
	struct blu_active_dbs_tuners *tuners = dbs_data->tuners;
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
	struct blu_active_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input < 1 || input > 100 ||
	    input >= dbs_data->up_threshold)
		return -EINVAL;

	tuners->down_threshold = input;
	return count;
}

static ssize_t store_hispeed_pct(struct gov_attr_set *attr_set,
				 const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct blu_active_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->hispeed_pct = input;
	return count;
}

static ssize_t store_ramp_step(struct gov_attr_set *attr_set, const char *buf,
			       size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct blu_active_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->ramp_step = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(min_sampling_rate);
gov_show_one(blu_active, down_threshold);
gov_show_one(blu_active, hispeed_pct);
gov_show_one(blu_active, ramp_step);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold);
gov_attr_rw(hispeed_pct);
gov_attr_rw(ramp_step);

static struct attribute *blu_active_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&hispeed_pct.attr,
	&ramp_step.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *blu_active_alloc(void)
{
	struct blu_active_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void blu_active_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int blu_active_init(struct dbs_data *dbs_data)
{
	struct blu_active_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->hispeed_pct = BLU_DEF_HISPEED_PCT;
	tuners->down_threshold = BLU_DEF_DOWN_THRESHOLD;
	tuners->ramp_step = BLU_DEF_RAMP_STEP;
	dbs_data->up_threshold = BLU_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void blu_active_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void blu_active_start(struct cpufreq_policy *policy)
{
	struct blu_active_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
	dbs_info->hispeed_active = false;
}

struct dbs_governor blu_active_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("blu_active"),
	.kobj_type = { .default_attrs = blu_active_attributes },
	.gov_dbs_timer = blu_active_dbs_timer,
	.alloc = blu_active_alloc,
	.free = blu_active_free,
	.init = blu_active_init,
	.exit = blu_active_exit,
	.start = blu_active_start,
};

#define CPU_FREQ_GOV_BLU_ACTIVE	(&blu_active_governor.gov)

static int __init cpufreq_gov_blu_active_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_BLU_ACTIVE);
}

static void __exit cpufreq_gov_blu_active_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_BLU_ACTIVE);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_blu_active' - hispeed-first ramp governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_blu_active_init);
module_exit(cpufreq_gov_blu_active_exit);
