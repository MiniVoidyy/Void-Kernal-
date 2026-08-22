/*
 * drivers/cpufreq/cpufreq_lionfish.c
 *
 * 'lionfish' - conservative-style stepping governor with a hispeed floor
 * that stays engaged until load drops below the down threshold.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct lionfish_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int requested_freq;
	bool floor_active;
};

static inline struct lionfish_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct lionfish_policy_dbs_info, policy_dbs);
}

struct lionfish_dbs_tuners {
	unsigned int down_threshold;
	unsigned int freq_step;
	unsigned int floor_pct;
};

#define LIONFISH_DEF_UP_THRESHOLD	(80)
#define LIONFISH_DEF_DOWN_THRESHOLD	(30)
#define LIONFISH_DEF_FREQ_STEP		(8)
#define LIONFISH_DEF_FLOOR_PCT		(70)

static unsigned int lionfish_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct lionfish_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	unsigned int requested_freq = dbs_info->requested_freq;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct lionfish_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	unsigned int freq_target = (tuners->freq_step * policy->max) / 100;
	unsigned int floor_freq =
		(tuners->floor_pct * policy->max) / 100;

	if (unlikely(freq_target == 0))
		freq_target = policy->max / 100;

	if (requested_freq > policy->max || requested_freq < policy->min)
		requested_freq = policy->cur;

	if (load > dbs_data->up_threshold) {
		dbs_info->floor_active = true;
		requested_freq += freq_target;
		if (requested_freq > policy->max)
			requested_freq = policy->max;
		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_H);
		dbs_info->requested_freq = requested_freq;
		goto out;
	}

	if (load < tuners->down_threshold)
		dbs_info->floor_active = false;

	if (dbs_info->floor_active && requested_freq < floor_freq)
		requested_freq = floor_freq;
	else if (!dbs_info->floor_active &&
		 load < tuners->down_threshold)
		requested_freq -= freq_target;

	if ((int)requested_freq < policy->min)
		requested_freq = policy->min;

	if (requested_freq != policy->cur) {
		__cpufreq_driver_target(policy, requested_freq,
					CPUFREQ_RELATION_L);
		dbs_info->requested_freq = requested_freq;
	} else {
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
	struct lionfish_dbs_tuners *tuners = dbs_data->tuners;
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
	struct lionfish_dbs_tuners *tuners = dbs_data->tuners;
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
	struct lionfish_dbs_tuners *tuners = dbs_data->tuners;
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

static ssize_t store_floor_pct(struct gov_attr_set *attr_set, const char *buf,
			       size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct lionfish_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->floor_pct = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(min_sampling_rate);
gov_show_one(lionfish, down_threshold);
gov_show_one(lionfish, freq_step);
gov_show_one(lionfish, floor_pct);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(down_threshold);
gov_attr_rw(freq_step);
gov_attr_rw(floor_pct);

static struct attribute *lionfish_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&freq_step.attr,
	&floor_pct.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *lionfish_alloc(void)
{
	struct lionfish_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void lionfish_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int lionfish_init(struct dbs_data *dbs_data)
{
	struct lionfish_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->down_threshold = LIONFISH_DEF_DOWN_THRESHOLD;
	tuners->freq_step = LIONFISH_DEF_FREQ_STEP;
	tuners->floor_pct = LIONFISH_DEF_FLOOR_PCT;
	dbs_data->up_threshold = LIONFISH_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void lionfish_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void lionfish_start(struct cpufreq_policy *policy)
{
	struct lionfish_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->requested_freq = policy->cur;
	dbs_info->floor_active = false;
}

struct dbs_governor lionfish_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("lionfish"),
	.kobj_type = { .default_attrs = lionfish_attributes },
	.gov_dbs_timer = lionfish_dbs_timer,
	.alloc = lionfish_alloc,
	.free = lionfish_free,
	.init = lionfish_init,
	.exit = lionfish_exit,
	.start = lionfish_start,
};

#define CPU_FREQ_GOV_LIONFISH	(&lionfish_governor.gov)

static int __init cpufreq_gov_lionfish_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_LIONFISH);
}

static void __exit cpufreq_gov_lionfish_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_LIONFISH);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_lionfish' - stepped governor with hispeed floor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_lionfish_init);
module_exit(cpufreq_gov_lionfish_exit);
