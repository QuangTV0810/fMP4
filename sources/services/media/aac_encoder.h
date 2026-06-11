#ifndef AAC_ENCODER_H
#define AAC_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AAC_ENCODER_ASC_MAX_SIZE 64

typedef enum {
    AAC_ENCODER_TRANSPORT_RAW = 0,  /* Raw AAC access unit for MP4/fMP4 muxing */
    AAC_ENCODER_TRANSPORT_ADTS = 2, /* ADTS-framed AAC stream */
} aac_encoder_transport_t;

typedef struct {
    int sample_rate; /* example: 8000, 16000, 44100, 48000 */
    int channels;    /* 1 = mono, 2 = stereo */
    int bitrate;     /* bits/sec, example: 32000, 64000 */
    int afterburner; /* 0 = off, 1 = better quality but more CPU */
    aac_encoder_transport_t transport;
} aac_encoder_config_t;

typedef struct {
    int sample_rate;
    int channels;
    int bitrate;
    int frame_length;  /* PCM samples per channel per AAC frame, usually 1024 for AAC-LC */
    int input_samples; /* frame_length * channels */
    int max_output_bytes;

    /*
     * ASC = AudioSpecificConfig.
     * Required by MP4/fMP4 muxers when transport = RAW.
     */
    uint8_t asc[AAC_ENCODER_ASC_MAX_SIZE];
    int asc_size;
} aac_encoder_info_t;

typedef struct aac_encoder aac_encoder_t;

/*
 * Create and initialize encoder handle with config.
 */
aac_encoder_t* aac_encoder_create(const aac_encoder_config_t* config);

/*
 * Destroy encoder.
 */
void aac_encoder_destroy(aac_encoder_t* encoder);

/*
 * Get current config/info.
 */
int aac_encoder_get_config(aac_encoder_t* encoder, aac_encoder_config_t* config, aac_encoder_info_t* info);

/*
 * Encode PCM signed 16-bit interleaved to AAC.
 *
 * pcm:
 *   PCM s16le interleaved.
 *   Mono:   S0 S1 S2...
 *   Stereo: L0 R0 L1 R1...
 *
 * pcm_samples:
 *   Total number of input samples across all channels.
 *   Example: mono, frame_length = 1024   -> pcm_samples = 1024
 *   Example: stereo, frame_length = 1024 -> pcm_samples = 2048
 *
 * out:
 *   Output buffer for either a raw AAC access unit or an ADTS frame,
 *   depending on config.transport.
 *
 * out_size:
 *   Size of the output buffer in bytes.
 *
 * out_bytes:
 *   Number of AAC bytes produced.
 *
 * consumed_samples:
 *   Number of PCM samples consumed across all channels.
 *
 * Return:
 *   0: OK
 *  >0: OK but no AAC output yet, for example not enough PCM has been accumulated
 *  <0: error
 */
int aac_encoder_encode(aac_encoder_t* encoder, const int16_t* pcm, int pcm_samples, uint8_t* out, int out_size,
                       int* out_bytes, int* consumed_samples);

/*
 * Flush encoder at end of stream.
 * Call repeatedly until the function returns 1.
 *
 * Return:
 *   0: flush step completed, more output may still be pending
 *   1: flush done / EOF
 *  <0: error
 */
int aac_encoder_flush(aac_encoder_t* encoder, uint8_t* out, int out_size, int* out_bytes);

#ifdef __cplusplus
}
#endif

#endif /* AAC_ENCODER_H */
