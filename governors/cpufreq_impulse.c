/*
 * drivers/cpufreq/cpufreq_impulse.c
 *
 * 'impulse' - spike-reactive governor: a heavy-load sample pulses straight
 * to max and holds there for a configurable number of samples before
 * decaying again.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct impulse_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
	unsigned int hold_counter;
};

static inline struct impulse_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct impulse_policy_dbs_info, policy_dbs);
}

struct impulse_dbs_tuners {
	unsigned int boost_hold_samples;
	unsigned int decay_step_pct;
};

#define IMPULSE_DEF_UP_THRESHOLD	(95)
#define IMPULSE_DEF_BOOST_HOLD		(8)
#define IMPULSE_DEF_DECAY_STEP_PCT	(15)

static unsigned int impulse_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct impulse_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct impulse_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);

	if (requested_freq > policy->max || requested_freq < policy->min)
		requested_freq = policy->cur;

	if (load >= dbs_data->up_threshold) {
		dbs_info->hold_counter = tuners->boost_hold_samples;
		requested_freq = policy->max;
		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
		goto out;
	}

	if (dbs_info->hold_counter) {
		dbs_info->hold_counter--;
		goto out;
	}

	if (requested_freq > policy->min) {
		requested_freq -=
			(tuners->decay_step_pct * policy->max) / 100;
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
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 50)
		return -EINVAL;

	dbs_data->up_threshold = input;
	return count;
}

static ssize_t store_boost_hold_samples(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct impulse_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 1000 || input < 1)
		return -EINVAL;

	tuners->boost_hold_samples = input;
	return count;
}

static ssize_t store_decay_step_pct(struct gov_attr_set *attr_set,
				    const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct impulse_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->decay_step_pct = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(min_sampling_rate);
gov_show_one(impulse, boost_hold_samples);
gov_show_one(impulse, decay_step_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(boost_hold_samples);
gov_attr_rw(decay_step_pct);

static struct attribute *impulse_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&boost_hold_samples.attr,
	&decay_step_pct.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *impulse_alloc(void)
{
	struct impulse_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void impulse_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int impulse_init(struct dbs_data *dbs_data)
{
	struct impulse_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->boost_hold_samples = IMPULSE_DEF_BOOST_HOLD;
	tuners->decay_step_pct = IMPULSE_DEF_DECAY_STEP_PCT;
	dbs_data->up_threshold = IMPULSE_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void impulse_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void impulse_start(struct cpufreq_policy *policy)
{
	struct impulse_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
	dbs_info->hold_counter = 0;
}

struct dbs_governor impulse_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("impulse"),
	.kobj_type = { .default_attrs = impulse_attributes },
	.gov_dbs_timer = impulse_dbs_timer,
	.alloc = impulse_alloc,
	.free = impulse_free,
	.init = impulse_init,
	.exit = impulse_exit,
	.start = impulse_start,
};

#define CPU_FREQ_GOV_IMPULSE	(&impulse_governor.gov)

static int __init cpufreq_gov_impulse_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_IMPULSE);
}

static void __exit cpufreq_gov_impulse_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_IMPULSE);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_impulse' - pulse-to-max with hold timer");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_impulse_init);
module_exit(cpufreq_gov_impulse_exit);
