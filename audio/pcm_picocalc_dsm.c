/*
 * ALSA plugin "picocalc_dsm": takes normal 16-bit mono/stereo audio and turns
 * it into a 1-bit delta-sigma stream for the PicoCalc speaker, sent out of the
 * Radxa Zero 3W's I2S1 SDO3 line (header pin 32).
 *
 * The I2S runs as 8 channels x 32 bits. Pin 32 (SDO3) carries channels 6 and
 * 7, i.e. 64 bits per frame, which we fill with modulator output. All other
 * channels are left silent (their pins aren't connected anyway).
 *
 * asound.conf usage:
 *   pcm.picocalc { type picocalc_dsm  slave.pcm "hw:CARD=PicoCalcI2S"
 *                  gain 1.0  swap false }
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include "dsm_core.h"

typedef struct {
	snd_pcm_extplug_t ext;
	dsm_state st;
	float gain;
	int swap;
} picocalc_dsm_t;

static inline void *area_ptr(const snd_pcm_channel_area_t *a,
			     snd_pcm_uframes_t frame)
{
	return (char *)a->addr + (a->first + a->step * frame) / 8;
}

static snd_pcm_sframes_t
dsm_transfer(snd_pcm_extplug_t *ext,
	     const snd_pcm_channel_area_t *dst_areas,
	     snd_pcm_uframes_t dst_offset,
	     const snd_pcm_channel_area_t *src_areas,
	     snd_pcm_uframes_t src_offset,
	     snd_pcm_uframes_t size)
{
	picocalc_dsm_t *d = ext->private_data;
	unsigned int in_ch = ext->channels;
	unsigned int out_ch = ext->slave_channels;
	snd_pcm_uframes_t f;
	unsigned int c;

	for (f = 0; f < size; f++) {
		float x = 0.0f;
		uint32_t w0, w1;

		for (c = 0; c < in_ch; c++)
			x += *(int16_t *)area_ptr(&src_areas[c], src_offset + f);
		x = x / (32768.0f * in_ch) * d->gain;

		dsm_frame(&d->st, x, &w0, &w1);
		if (d->swap) {
			uint32_t t = w0;
			w0 = w1;
			w1 = t;
		}

		for (c = 0; c < out_ch; c++) {
			int32_t *q = area_ptr(&dst_areas[c], dst_offset + f);

			if (c == out_ch - 2)
				*q = (int32_t)w0;
			else if (c == out_ch - 1)
				*q = (int32_t)w1;
			else
				*q = 0;
		}
	}
	return size;
}

static int dsm_init(snd_pcm_extplug_t *ext)
{
	picocalc_dsm_t *d = ext->private_data;

	dsm_reset(&d->st);
	return 0;
}

static int dsm_close(snd_pcm_extplug_t *ext)
{
	free(ext->private_data);
	return 0;
}

static const snd_pcm_extplug_callback_t dsm_callback = {
	.transfer = dsm_transfer,
	.init = dsm_init,
	.close = dsm_close,
};

SND_PCM_PLUGIN_DEFINE_FUNC(picocalc_dsm)
{
	snd_config_iterator_t i, next;
	snd_config_t *slave = NULL;
	picocalc_dsm_t *d;
	double gain = 1.0;
	int swap = 0;
	int err;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;

		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (!strcmp(id, "comment") || !strcmp(id, "type") ||
		    !strcmp(id, "hint"))
			continue;
		if (!strcmp(id, "slave")) {
			slave = n;
			continue;
		}
		if (!strcmp(id, "gain")) {
			if (snd_config_get_ireal(n, &gain) < 0) {
				SNDERR("gain must be a number");
				return -EINVAL;
			}
			continue;
		}
		if (!strcmp(id, "swap")) {
			swap = snd_config_get_bool(n);
			if (swap < 0) {
				SNDERR("swap must be true or false");
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) {
		SNDERR("No slave defined for picocalc_dsm");
		return -EINVAL;
	}

	d = calloc(1, sizeof(*d));
	if (!d)
		return -ENOMEM;
	d->gain = (float)gain;
	d->swap = swap;
	dsm_reset(&d->st);

	d->ext.version = SND_PCM_EXTPLUG_VERSION;
	d->ext.name = "PicoCalc 1-bit delta-sigma";
	d->ext.callback = &dsm_callback;
	d->ext.private_data = d;

	err = snd_pcm_extplug_create(&d->ext, name, root, slave, stream, mode);
	if (err < 0) {
		free(d);
		return err;
	}

	snd_pcm_extplug_set_param_minmax(&d->ext, SND_PCM_EXTPLUG_HW_CHANNELS,
					 1, 2);
	snd_pcm_extplug_set_slave_param(&d->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 8);
	snd_pcm_extplug_set_param(&d->ext, SND_PCM_EXTPLUG_HW_FORMAT,
				  SND_PCM_FORMAT_S16);
	snd_pcm_extplug_set_slave_param(&d->ext, SND_PCM_EXTPLUG_HW_FORMAT,
					SND_PCM_FORMAT_S32);

	*pcmp = d->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(picocalc_dsm);
