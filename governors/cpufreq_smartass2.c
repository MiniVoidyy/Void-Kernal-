/*
 * drivers/cpufreq/cpufreq_smartass2.c
 *
 * 'smartassV2' - screen-state aware governor with separate awake/asleep
 * behaviour: while the display is off frequency is capped and decay is
 * accelerated to save power.
 * GPLv2
 */

#include <linux/slab.h>
#include <linux/fb.h>
#include "cpufreq_governor.h"

struct smartass2_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
};

static inline struct smartass2_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct smartass2_policy_dbs_info, policy_dbs);
}

struct smartass2_dbs_tuners {
	unsigned int down_threshold;
	unsigned int freq_step;
	unsigned int sleep_max_pct;
};

#define SMARTASS2_DEF_UP_THRESHOLD	(65)
#define SMARTASS2_DEF_DOWN_THRESHOLD	(35)
#define SMARTASS2_DEF_FREQ_STEP		(10)
#define SMARTASS2_DEF_SLEEP_MAX_PCT	(33)

static atomic_t screen_awake = ATOMIC_INIT(1);

static int smartass2_fb_notifier(struct notifier_block *nb,
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

static struct notifier_block smartass2_fb_nb = {
	.notifier_call = smartass2_fb_notifier,
};

static unsigned int smartass2_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct smartass2_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct smartass2_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	bool awake = atomic_read(&screen_awake) != 0;
	unsigned int freq_target =
		(tuners->freq_step * policy->max) / 100;

	if (requested_freq > policy->max || requested_freq < policy->min)
		requested_freq = policy->cur;

	if (load > dbs_data->up_threshold && awake) {
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
	struct smartass2_dbs_tuners *tuners = dbs_data->tuners;
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
	struct smartass2_dbs_tuners *tuners = dbs_data->tuners;
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
	struct smartass2_dbs_tuners *tuners = dbs_data->tuners;
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
	struct smartass2_dbs_tuners *tuners = dbs_data->tuners;
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
gov_show_one(smartass2, down_threshold);
gov_show_one(smartass2, freq_step);
gov_show_one(smartass2, sleep_max_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold);
gov_attr_rw(freq_step);
gov_attr_rw(sleep_max_pct);

static struct attribute *smartass2_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&freq_step.attr,
	&sleep_max_pct.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *smartass2_alloc(void)
{
	struct smartass2_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void smartass2_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int smartass2_init(struct dbs_data *dbs_data)
{
	struct smartass2_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->down_threshold = SMARTASS2_DEF_DOWN_THRESHOLD;
	tuners->freq_step = SMARTASS2_DEF_FREQ_STEP;
	tuners->sleep_max_pct = SMARTASS2_DEF_SLEEP_MAX_PCT;
	dbs_data->up_threshold = SMARTASS2_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void smartass2_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void smartass2_start(struct cpufreq_policy *policy)
{
	struct smartass2_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
}

struct dbs_governor smartass2_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("smartassV2"),
	.kobj_type = { .default_attrs = smartass2_attributes },
	.gov_dbs_timer = smartass2_dbs_timer,
	.alloc = smartass2_alloc,
	.free = smartass2_free,
	.init = smartass2_init,
	.exit = smartass2_exit,
	.start = smartass2_start,
};

#define CPU_FREQ_GOV_SMARTASS2	(&smartass2_governor.gov)

static int __init cpufreq_gov_smartass2_init(void)
{
	int ret;

	ret = fb_register_client(&smartass2_fb_nb);
	if (ret)
		return ret;

	ret = cpufreq_register_governor(CPU_FREQ_GOV_SMARTASS2);
	if (ret)
		fb_unregister_client(&smartass2_fb_nb);

	return ret;
}

static void __exit cpufreq_gov_smartass2_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_SMARTASS2);
	fb_unregister_client(&smartass2_fb_nb);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_smartass2' - screen-aware governor with sleep cap");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_smartass2_init);
module_exit(cpufreq_gov_smartass2_exit);
