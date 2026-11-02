// SPDX-License-Identifier: GPL-2.0
/*
 * picocalc_dsm_codec - stand-in "codec" for the PicoCalc I2S speaker path.
 *
 * The PicoCalc has no audio chip on the I2S line: header pin 32 (I2S1 SDO3)
 * feeds its RC filter and amplifier directly with a 1-bit delta-sigma stream
 * made in userspace. ASoC still wants a codec on the other end of the link,
 * so this one just accepts whatever the I2S controller sends.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>

static struct snd_soc_dai_driver pc_dsm_dai = {
	.name = "picocalc-dsm",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
};

static const struct snd_soc_component_driver pc_dsm_component = {
	.endianness = 1,
};

static int pc_dsm_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &pc_dsm_component,
					       &pc_dsm_dai, 1);
}

static const struct of_device_id pc_dsm_of_match[] = {
	{ .compatible = "picocalc,dsm-codec" },
	{ }
};
MODULE_DEVICE_TABLE(of, pc_dsm_of_match);

static struct platform_driver pc_dsm_driver = {
	.probe = pc_dsm_probe,
	.driver = {
		.name = "picocalc-dsm-codec",
		.of_match_table = pc_dsm_of_match,
	},
};
module_platform_driver(pc_dsm_driver);

MODULE_DESCRIPTION("PicoCalc 1-bit I2S stand-in codec");
MODULE_LICENSE("GPL");
