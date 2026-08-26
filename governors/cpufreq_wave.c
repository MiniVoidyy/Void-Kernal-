/*
 * drivers/cpufreq/cpufreq_wave.c
 *
 * 'wave' - oscillating governor: frequency drifts up and down in waves
 * between the policy limits; heavy load forces the wave upward, light
 * load pulls it down.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct wave_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
	int dir_up;
	unsigned int sample_cnt;
};

static inline struct wave_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct wave_policy_dbs_info, policy_dbs);
}

struct wave_dbs_tuners {
	unsigned int down_threshold_pct;
	unsigned int wave_step_pct;
	unsigned int wave_interval;
};

#define WAVE_DEF_UP_THRESHOLD	(85)
#define WAVE_DEF_DOWN_THRESHOLD	(15)
#define WAVE_DEF_WAVE_STEP_PCT	(10)
#define WAVE_DEF_INTERVAL	(6)

static unsigned int wave_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct wave_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct wave_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	unsigned int step = (tuners->wave_step_pct * policy->max) / 100;

	if (unlikely(step == 0))
		step = policy->max / 100;

	if (requested_freq > policy->max || requested_freq < policy->min) {
		requested_freq = policy->cur;
		dbs_info->sample_cnt = tuners->wave_interval;
	}

	if (load > dbs_data->up_threshold)
		dbs_info->dir_up = 1;
	else if (load < tuners->down_threshold_pct)
		dbs_info->dir_up = 0;

	if (++dbs_info->sample_cnt >= tuners->wave_interval) {
		dbs_info->sample_cnt = 0;

		if (dbs_info->dir_up) {
			requested_freq += step;
			if (requested_freq >= policy->max) {
				requested_freq = policy->max;
				dbs_info->dir_up = 0;
			}
		} else {
			if (requested_freq > step)
				requested_freq -= step;
			else
				requested_freq = policy->min;

			if (requested_freq <= policy->min) {
				requested_freq = policy->min;
				dbs_info->dir_up = 1;
			}
		}

		if (requested_freq != policy->cur)
			__cpufreq_driver_target(policy, requested_freq,
					load >= dbs_data->up_threshold ?
					CPUFREQ_RELATION_H : CPUFREQ_RELATION_L);
	}

	dbs_info->requested_freq = requested_freq;
	return dbs_data->sampling_rate;
}

/************************** sysfs interface ************************/

static ssize_t store_up_threshold(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct wave_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input <= tuners->down_threshold_pct)
		return -EINVAL;

	dbs_data->up_threshold = input;
	return count;
}

static ssize_t store_down_threshold_pct(struct gov_attr_set *attr_set,
				    const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct wave_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input < 1 || input > 100 ||
	    input >= dbs_data->up_threshold)
		return -EINVAL;

	tuners->down_threshold_pct = input;
	return count;
}

static ssize_t store_wave_step_pct(struct gov_attr_set *attr_set, const char *buf,
			       size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct wave_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->wave_step_pct = input;
	return count;
}

static ssize_t store_wave_interval(struct gov_attr_set *attr_set,
				   const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct wave_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 1000 || input < 1)
		return -EINVAL;

	tuners->wave_interval = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(min_sampling_rate);
gov_show_one(wave, down_threshold_pct);
gov_show_one(wave, wave_step_pct);
gov_show_one(wave, wave_interval);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold_pct);
gov_attr_rw(wave_step_pct);
gov_attr_rw(wave_interval);

static struct attribute *wave_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold_pct.attr,
	&wave_step_pct.attr,
	&wave_interval.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *wave_alloc(void)
{
	struct wave_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void wave_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int wave_init(struct dbs_data *dbs_data)
{
	struct wave_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->down_threshold_pct = WAVE_DEF_DOWN_THRESHOLD;
	tuners->wave_step_pct = WAVE_DEF_WAVE_STEP_PCT;
	tuners->wave_interval = WAVE_DEF_INTERVAL;
	dbs_data->up_threshold = WAVE_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void wave_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void wave_start(struct cpufreq_policy *policy)
{
	struct wave_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
	dbs_info->dir_up = 1;
	dbs_info->sample_cnt = 0;
}

struct dbs_governor wave_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("wave"),
	.kobj_type = { .default_attrs = wave_attributes },
	.gov_dbs_timer = wave_dbs_timer,
	.alloc = wave_alloc,
	.free = wave_free,
	.init = wave_init,
	.exit = wave_exit,
	.start = wave_start,
};

#define CPU_FREQ_GOV_WAVE	(&wave_governor.gov)

static int __init cpufreq_gov_wave_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_WAVE);
}

static void __exit cpufreq_gov_wave_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_WAVE);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_wave' - oscillating frequency governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_wave_init);
module_exit(cpufreq_gov_wave_exit);
