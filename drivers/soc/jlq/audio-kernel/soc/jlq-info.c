#include <soc/jlq-info.h>

struct snd_info_entry *snd_info_create_subdir(struct module *mod,
                                             const char *name,
                                             struct snd_info_entry *parent)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(mod, name, parent);
	if (!entry)
		return NULL;
	entry->mode = S_IFDIR | 0555;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return NULL;
	}
	return entry;
}
EXPORT_SYMBOL_GPL(snd_info_create_subdir);

