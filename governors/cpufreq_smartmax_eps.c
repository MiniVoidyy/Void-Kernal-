/*
 * drivers/cpufreq/cpufreq_smartmax_eps.c
 *
 * 'smartmax_eps' - extreme power save variant of smartmax: very slow
 * upward stepping, fast downward stepping and a deep sleep frequency cap.
 * GPLv2
 */

#include <linux/slab.h>
#include <linux/fb.h>
#include "cpufreq_governor.h"

struct smartmax_eps_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
};

static inline struct smartmax_eps_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct smartmax_eps_policy_dbs_info, policy_dbs);
}

struct smartmax_eps_dbs_tuners {
	unsigned int down_threshold;
	unsigned int up_step_pct;
	unsigned int down_step_pct;
	unsigned int sleep_max_pct;
};

#define EPS_DEF_UP_THRESHOLD	(90)
#define EPS_DEF_DOWN_THRESHOLD	(50)
#define EPS_DEF_UP_STEP_PCT	(3)
#define EPS_DEF_DOWN_STEP_PCT	(15)
#define EPS_DEF_SLEEP_MAX_PCT	(25)

static atomic_t screen_awake = ATOMIC_INIT(1);

static int smartmax_eps_fb_notifier(struct notifier_block *nb,
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

static struct notifier_block smartmax_eps_fb_nb = {
	.notifier_call = smartmax_eps_fb_notifier,
};

static unsigned int smartmax_eps_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct smartmax_eps_policy_dbs_info *dbs_info =
		to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct smartmax_eps_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	bool awake = atomic_read(&screen_awake) != 0;

	if (requested_freq > policy->max || requested_freq < policy->min)
		requested_freq = policy->cur;

	if (awake && load > dbs_data->up_threshold) {
		requested_freq += (tuners->up_step_pct * policy->max) / 100;
		if (requested_freq > policy->max)
			requested_freq = policy->max;
		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
		goto out;
	}

	if (load < tuners->down_threshold || !awake) {
		unsigned int step =
			(tuners->down_step_pct * policy->max) / 100;

		if (!awake)
			step *= 2;

		if (requested_freq > step)
			requested_freq -= step;
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
	struct smartmax_eps_dbs_tuners *tuners = dbs_data->tuners;
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
	struct smartmax_eps_dbs_tuners *tuners = dbs_data->tuners;
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
	struct smartmax_eps_dbs_tuners *tuners = dbs_data->tuners;
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
	struct smartmax_eps_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->down_step_pct = input;
	return count;
}

static ssize_t store_sleep_max_pct(struct gov_attr_set *attr_set,
				   const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct smartmax_eps_dbs_tuners *tuners = dbs_data->tuners;
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
gov_show_one(smartmax_eps, down_threshold);
gov_show_one(smartmax_eps, up_step_pct);
gov_show_one(smartmax_eps, down_step_pct);
gov_show_one(smartmax_eps, sleep_max_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold);
gov_attr_rw(up_step_pct);
gov_attr_rw(down_step_pct);
gov_attr_rw(sleep_max_pct);

static struct attribute *smartmax_eps_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&up_step_pct.attr,
	&down_step_pct.attr,
	&sleep_max_pct.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *smartmax_eps_alloc(void)
{
	struct smartmax_eps_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void smartmax_eps_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int smartmax_eps_init(struct dbs_data *dbs_data)
{
	struct smartmax_eps_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->down_threshold = EPS_DEF_DOWN_THRESHOLD;
	tuners->up_step_pct = EPS_DEF_UP_STEP_PCT;
	tuners->down_step_pct = EPS_DEF_DOWN_STEP_PCT;
	tuners->sleep_max_pct = EPS_DEF_SLEEP_MAX_PCT;
	dbs_data->up_threshold = EPS_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void smartmax_eps_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void smartmax_eps_start(struct cpufreq_policy *policy)
{
	struct smartmax_eps_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
}

struct dbs_governor smartmax_eps_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("smartmax_eps"),
	.kobj_type = { .default_attrs = smartmax_eps_attributes },
	.gov_dbs_timer = smartmax_eps_dbs_timer,
	.alloc = smartmax_eps_alloc,
	.free = smartmax_eps_free,
	.init = smartmax_eps_init,
	.exit = smartmax_eps_exit,
	.start = smartmax_eps_start,
};

#define CPU_FREQ_GOV_SMARTMAX_EPS	(&smartmax_eps_governor.gov)

static int __init cpufreq_gov_smartmax_eps_init(void)
{
	int ret;

	ret = fb_register_client(&smartmax_eps_fb_nb);
	if (ret)
		return ret;

	ret = cpufreq_register_governor(CPU_FREQ_GOV_SMARTMAX_EPS);
	if (ret)
		fb_unregister_client(&smartmax_eps_fb_nb);

	return ret;
}

static void __exit cpufreq_gov_smartmax_eps_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_SMARTMAX_EPS);
	fb_unregister_client(&smartmax_eps_fb_nb);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_smartmax_eps' - extreme powersave governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_smartmax_eps_init);
module_exit(cpufreq_gov_smartmax_eps_exit);
