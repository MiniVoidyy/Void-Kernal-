/*
 * drivers/cpufreq/cpufreq_smartmax.c
 *
 * 'smartmax' - screen-aware conservative-style governor: stepped ramping
 * while awake, capped frequency and doubled decay steps while asleep.
 * GPLv2
 */

#include <linux/slab.h>
#include <linux/fb.h>
#include "cpufreq_governor.h"

struct smartmax_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
};

static inline struct smartmax_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct smartmax_policy_dbs_info, policy_dbs);
}

struct smartmax_dbs_tuners {
	unsigned int down_threshold;
	unsigned int freq_step;
	unsigned int sleep_max_pct;
};

#define SMARTMAX_DEF_UP_THRESHOLD	(75)
#define SMARTMAX_DEF_DOWN_THRESHOLD	(40)
#define SMARTMAX_DEF_FREQ_STEP		(12)
#define SMARTMAX_DEF_SLEEP_MAX_PCT	(40)

static atomic_t screen_awake = ATOMIC_INIT(1);

static int smartmax_fb_notifier(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct fb_event *fevent = data;
	int *blank;

	if (event != FB_EVENT_BLANK || !fevent)
		return NOTIFY_OK;

	blank = fevent->data;
	if (!blank)
		return NOTIFY_OK;

	atomic_set(&screen_awake, (*blank == FB_BLANK_UNBLANK) ? 1 : 0);
	return NOTIFY_OK;
}

static struct notifier_block smartmax_fb_nb = {
	.notifier_call = smartmax_fb_notifier,
};

static unsigned int smartmax_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct smartmax_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct smartmax_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	bool awake = atomic_read(&screen_awake) != 0;
	unsigned int freq_target =
		(tuners->freq_step * policy->max) / 100;

	if (requested_freq > policy->max || requested_freq < policy->min)
		requested_freq = policy->cur;

	if (awake && load > dbs_data->up_threshold) {
		requested_freq += freq_target;
		if (requested_freq > policy->max)
			requested_freq = policy->max;
		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
		goto out;
	}

	if (load < tuners->down_threshold || !awake) {
		if (!awake)
			freq_target *= 2;

		if (requested_freq > freq_target)
			requested_freq -= freq_target;
		else
			requested_freq = policy->min;

		if (!awake) {
			unsigned int cap =
				(tuners->sleep_max_pct * policy->max) / 100;

			if (cap < policy->min)
				cap = policy->min;
			if (requested_freq > cap)
				requested_freq = cap;
		} else if ((int)requested_freq < policy->min) {
			requested_freq = policy->min;
		}

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
	struct smartmax_dbs_tuners *tuners = dbs_data->tuners;
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
	struct smartmax_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input < 1 || input > 100 ||
	    input >= dbs_data->up_threshold)
		return -EINVAL;

	tuners->down_threshold = input;
	return count;
}

static ssize_t store_freq_step(struct gov_attr_set *attr_set, const char *buf,
			       size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct smartmax_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->freq_step = input;
	return count;
}

static ssize_t store_sleep_max_pct(struct gov_attr_set *attr_set,
				   const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct smartmax_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->sleep_max_pct = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(min_sampling_rate);
gov_show_one(smartmax, down_threshold);
gov_show_one(smartmax, freq_step);
gov_show_one(smartmax, sleep_max_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold);
gov_attr_rw(freq_step);
gov_attr_rw(sleep_max_pct);

static struct attribute *smartmax_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&freq_step.attr,
	&sleep_max_pct.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *smartmax_alloc(void)
{
	struct smartmax_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void smartmax_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int smartmax_init(struct dbs_data *dbs_data)
{
	struct smartmax_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->down_threshold = SMARTMAX_DEF_DOWN_THRESHOLD;
	tuners->freq_step = SMARTMAX_DEF_FREQ_STEP;
	tuners->sleep_max_pct = SMARTMAX_DEF_SLEEP_MAX_PCT;
	dbs_data->up_threshold = SMARTMAX_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void smartmax_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void smartmax_start(struct cpufreq_policy *policy)
{
	struct smartmax_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
}

struct dbs_governor smartmax_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("smartmax"),
	.kobj_type = { .default_attrs = smartmax_attributes },
	.gov_dbs_timer = smartmax_dbs_timer,
	.alloc = smartmax_alloc,
	.free = smartmax_free,
	.init = smartmax_init,
	.exit = smartmax_exit,
	.start = smartmax_start,
};

#define CPU_FREQ_GOV_SMARTMAX	(&smartmax_governor.gov)

static int __init cpufreq_gov_smartmax_init(void)
{
	int ret;

	ret = fb_register_client(&smartmax_fb_nb);
	if (ret)
		return ret;

	ret = cpufreq_register_governor(CPU_FREQ_GOV_SMARTMAX);
	if (ret)
		fb_unregister_client(&smartmax_fb_nb);

	return ret;
}

static void __exit cpufreq_gov_smartmax_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_SMARTMAX);
	fb_unregister_client(&smartmax_fb_nb);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_smartmax' - screen-aware stepped governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_smartmax_init);
module_exit(cpufreq_gov_smartmax_exit);
