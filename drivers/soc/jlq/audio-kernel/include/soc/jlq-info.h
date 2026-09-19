#include <sound/info.h>

struct snd_info_entry *snd_info_create_subdir(struct module *mod,
					      const char *name,
					      struct snd_info_entry *parent);

