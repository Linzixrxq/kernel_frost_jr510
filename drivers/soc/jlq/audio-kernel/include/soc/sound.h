#ifndef __JLQ_SOUND_H
#define __JLQ_SOUND_H

/* soundwire */
#define SOUNDWIRE_NAME_SIZE	32
#define SOUNDWIRE_MODULE_PREFIX "swr:"

struct swr_device_id {
	char name[SOUNDWIRE_NAME_SIZE];
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif  /* __JLQ_SOUND_H */
