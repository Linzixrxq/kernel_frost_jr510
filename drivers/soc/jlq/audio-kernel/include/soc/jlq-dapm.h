#include <sound/soc-dapm.h>
#include <linux/regmap.h>

struct swr_device;


#define SND_SOC_DAPM_MICBIAS_E(wname, wreg, wshift, winvert, wevent, wflags) \
{	.id = snd_soc_dapm_micbias, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = NULL, .num_kcontrols = 0, \
	.event = wevent, .event_flags = wflags}

struct regmap *__devm_regmap_init_swr(struct swr_device *swr,
					const struct regmap_config *config,
					struct lock_class_key *lock_key,
					const char *lock_name);

#define devm_regmap_init_swr(swr, config)				\
		__regmap_lockdep_wrapper(__devm_regmap_init_swr, #config,	\
					swr, config)


