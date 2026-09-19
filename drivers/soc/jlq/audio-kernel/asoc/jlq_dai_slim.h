#ifndef __JLQ_DAI_SLIM_H
#define __JLQ_DAI_SLIM_H

int jlq_dai_slim_startup(struct snd_pcm_substream *substream,
                         struct snd_soc_dai *dai);

int jlq_dai_slim_hw_params(
    struct snd_pcm_substream *substream,
    struct snd_pcm_hw_params *params,
    struct snd_soc_dai *dai);
int jlq_dai_slim_set_channel_map(struct snd_soc_dai *dai,
                                 unsigned int tx_num, unsigned int *tx_slot,
                                 unsigned int rx_num, unsigned int *rx_slot);
int jlq_dai_slim_prepare(struct snd_pcm_substream *substream,
                         struct snd_soc_dai *dai);
void jlq_dai_slim_shutdown(struct snd_pcm_substream *substream,
                           struct snd_soc_dai *dai);
#endif /* __JLQ_DAI_SLIM_H */

