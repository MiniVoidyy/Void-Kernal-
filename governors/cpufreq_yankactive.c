/*
 * drivers/cpufreq/cpufreq_yankactive.c
 *
 * 'yankactive' - interactive-flavoured governor with dynamic sampling:
 * busy periods are re-sampled four times faster, idle periods slower.
 * GPLv2
 */

#include <linux/slab.h>
#include "cpufreq_governor.h"

struct yankactive_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int target_freq;
};

static inline struct yankactive_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct yankactive_policy_dbs_info, policy_dbs);
}

struct yankactive_dbs_tuners {
	unsigned int hispeed_pct;
	unsigned int busy_div;
	unsigned int idle_mult;
};

#define YA_DEF_UP_THRESHOLD	(75)
#define YA_DEF_HISPEED_PCT	(85)
#define YA_DEF_BUSY_DIV		(4)
#define YA_DEF_IDLE_MULT	(2)

static unsigned int yankactive_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct yankactive_policy_dbs_info *dbs_info =
		to_dbs_info(policy_dbs);
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct yankactive_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);
	unsigned int hispeed_freq =
		(tuners->hispeed_pct * policy->max) / 100;
	unsigned int next_timer;

	if (load > dbs_data->up_threshold) {
		dbs_info->target_freq = dbs_info->target_freq >= hispeed_freq ?
			policy->max : hispeed_freq;
		__cpufreq_driver_target(policy, dbs_info->target_freq,
					CPUFREQ_RELATION_H);
		next_timer = dbs_data->sampling_rate /
			     max(tuners->busy_div, 1U);
	} else if (load < 5) {
		dbs_info->target_freq -= (15 * policy->max) / 100;
		if ((int)dbs_info->target_freq < policy->min)
			dbs_info->target_freq = policy->min;
		__cpufreq_driver_target(policy, dbs_info->target_freq,
					CPUFREQ_RELATION_L);
		next_timer = dbs_data->sampling_rate *
			     max(tuners->idle_mult, 1U);
	} else {
		next_timer = dbs_data->sampling_rate;
	}

	if (next_timer < dbs_data->min_sampling_rate)
		next_timer = dbs_data->min_sampling_rate;

	return next_timer;
}

/************************** sysfs interface ************************/

static ssize_t store_up_threshold(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 6)
		return -EINVAL;

	dbs_data->up_threshold = input;
	return count;
}

static ssize_t store_hispeed_pct(struct gov_attr_set *attr_set,
				 const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct yankactive_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 100 || input < 1)
		return -EINVAL;

	tuners->hispeed_pct = input;
	return count;
}

static ssize_t store_busy_div(struct gov_attr_set *attr_set, const char *buf,
			      size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct yankactive_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 10 || input < 1)
		return -EINVAL;

	tuners->busy_div = input;
	return count;
}

static ssize_t store_idle_mult(struct gov_attr_set *attr_set, const char *buf,
			       size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct yankactive_dbs_tuners *tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 10 || input < 1)
		return -EINVAL;

	tuners->idle_mult = input;
	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(min_sampling_rate);
gov_show_one(yankactive, hispeed_pct);
gov_show_one(yankactive, busy_div);
gov_show_one(yankactive, idle_mult);

gov_attr_rw(sampling_rate);
gov_attr_rw(up_threshold);
gov_attr_ro(min_sampling_rate);
gov_attr_rw(hispeed_pct);
gov_attr_rw(busy_div);
gov_attr_rw(idle_mult);

static struct attribute *yankactive_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&hispeed_pct.attr,
	&busy_div.attr,
	&idle_mult.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *yankactive_alloc(void)
{
	struct yankactive_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void yankactive_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int yankactive_init(struct dbs_data *dbs_data)
{
	struct yankactive_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	tuners->hispeed_pct = YA_DEF_HISPEED_PCT;
	tuners->busy_div = YA_DEF_BUSY_DIV;
	tuners->idle_mult = YA_DEF_IDLE_MULT;
	dbs_data->up_threshold = YA_DEF_UP_THRESHOLD;
	dbs_data->sampling_down_factor = 1;
	dbs_data->ignore_nice_load = 0;
	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);

	return 0;
}

static void yankactive_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void yankactive_start(struct cpufreq_policy *policy)
{
	struct yankactive_policy_dbs_info *dbs_info =
		to_dbs_info(policy->governor_data);

	dbs_info->target_freq = policy->cur;
}

struct dbs_governor yankactive_governor = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("yankactive"),
	.kobj_type = { .default_attrs = yankactive_attributes },
	.gov_dbs_timer = yankactive_dbs_timer,
	.alloc = yankactive_alloc,
	.free = yankactive_free,
	.init = yankactive_init,
	.exit = yankactive_exit,
	.start = yankactive_start,
};

#define CPU_FREQ_GOV_YANKACTIVE	(&yankactive_governor.gov)

static int __init cpufreq_gov_yankactive_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_YANKACTIVE);
}

static void __exit cpufreq_gov_yankactive_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_YANKACTIVE);
}

MODULE_AUTHOR("Mini_Voidyy <Void Kernel>");
MODULE_DESCRIPTION("'cpufreq_yankactive' - dynamic-sampling interactive governor");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_yankactive_init);
module_exit(cpufreq_gov_yankactive_exit);
