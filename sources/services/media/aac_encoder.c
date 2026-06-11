#include "aac_encoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fdk-aac/aacenc_lib.h>

struct aac_encoder {
    HANDLE_AACENCODER handle;
    aac_encoder_config_t config;
    aac_encoder_info_t info;
    int configured;
};

static CHANNEL_MODE aac_encoder_channel_mode(int channels) {
    switch (channels) {
        case 1: return MODE_1;
        case 2: return MODE_2;
        case 3: return MODE_1_2;
        case 4: return MODE_1_2_1;
        case 5: return MODE_1_2_2;
        case 6: return MODE_1_2_2_1;
        default: return MODE_INVALID;
    }
}

static int aac_encoder_validate_config(const aac_encoder_config_t* config) {
    if (config == NULL) {
        return -1;
    }

    if (config->sample_rate <= 0) {
        return -2;
    }

    if (config->channels <= 0 || config->channels > 6) {
        return -3;
    }

    if (aac_encoder_channel_mode(config->channels) == MODE_INVALID) {
        return -4;
    }

    if (config->bitrate <= 0) {
        return -5;
    }

    if (config->transport != AAC_ENCODER_TRANSPORT_RAW && config->transport != AAC_ENCODER_TRANSPORT_ADTS) {
        return -6;
    }

    if (config->afterburner != 0 && config->afterburner != 1) {
        return -7;
    }

    return 0;
}

static int aac_encoder_apply_config(aac_encoder_t* encoder, const aac_encoder_config_t* config) {
    AACENC_ERROR err;
    CHANNEL_MODE ch_mode;

    ch_mode = aac_encoder_channel_mode(config->channels);

    /*
     * AAC-LC.
     * If HE-AAC is needed later, switch AOT_AAC_LC to AOT_SBR/AOT_PS
     * and configure the related SBR parameters as well.
     */
    err = aacEncoder_SetParam(encoder->handle, AACENC_AOT, AOT_AAC_LC);
    if (err != AACENC_OK) {
        return -10;
    }

    err = aacEncoder_SetParam(encoder->handle, AACENC_SAMPLERATE, (UINT)config->sample_rate);
    if (err != AACENC_OK) {
        return -11;
    }

    err = aacEncoder_SetParam(encoder->handle, AACENC_CHANNELMODE, (UINT)ch_mode);
    if (err != AACENC_OK) {
        return -12;
    }

    err = aacEncoder_SetParam(encoder->handle, AACENC_BITRATE, (UINT)config->bitrate);
    if (err != AACENC_OK) {
        return -13;
    }

    /*
     * RAW:
     *   Output is a raw AAC access unit without an ADTS header.
     *   Use this for MP4/fMP4 muxers.
     *
     * ADTS:
     *   Output includes an ADTS header.
     *   Use this for .aac files or ADTS streaming.
     */
    err = aacEncoder_SetParam(encoder->handle, AACENC_TRANSMUX, (UINT)config->transport);
    if (err != AACENC_OK) {
        return -14;
    }

    err = aacEncoder_SetParam(encoder->handle, AACENC_AFTERBURNER, (UINT)config->afterburner);
    if (err != AACENC_OK) {
        return -15;
    }

    /*
     * Initialize or reconfigure the encoder with the current parameters.
     * FDK-AAC requires aacEncEncode(NULL, ...) after SetParam().
     */
    err = aacEncEncode(encoder->handle, NULL, NULL, NULL, NULL);
    if (err != AACENC_OK) {
        return -16;
    }

    return 0;
}

static int aac_encoder_update_info(aac_encoder_t* encoder) {
    AACENC_ERROR err;
    AACENC_InfoStruct enc_info;

    memset(&enc_info, 0, sizeof(enc_info));

    err = aacEncInfo(encoder->handle, &enc_info);
    if (err != AACENC_OK) {
        return -20;
    }

    memset(&encoder->info, 0, sizeof(encoder->info));

    encoder->info.sample_rate = encoder->config.sample_rate;
    encoder->info.channels = (int)enc_info.inputChannels;
    encoder->info.bitrate = encoder->config.bitrate;
    encoder->info.frame_length = (int)enc_info.frameLength;
    encoder->info.input_samples = (int)(enc_info.frameLength * enc_info.inputChannels);

    /*
     * FDK-AAC recommends an output buffer size around 6144 bits/channel.
     * 6144 bits = 768 bytes.
     * Add a small margin for safety.
     */
    encoder->info.max_output_bytes = 768 * encoder->config.channels + 128;

    if (enc_info.confSize > 0 && enc_info.confSize <= AAC_ENCODER_ASC_MAX_SIZE) {
        memcpy(encoder->info.asc, enc_info.confBuf, enc_info.confSize);
        encoder->info.asc_size = (int)enc_info.confSize;
    }

    return 0;
}

aac_encoder_t* aac_encoder_create(const aac_encoder_config_t* config) {
    AACENC_ERROR err;
    int ret;

    if (config == NULL) {
        return NULL;
    }

    ret = aac_encoder_validate_config(config);
    if (ret != 0) {
        return NULL;
    }

    aac_encoder_t* encoder = (aac_encoder_t*)calloc(1, sizeof(*encoder));
    if (encoder == NULL) {
        return NULL;
    }

    err = aacEncOpen(&encoder->handle, 0, (UINT)config->channels);
    if (err != AACENC_OK || encoder->handle == NULL) {
        free(encoder);
        return NULL;
    }

    encoder->config = *config;

    ret = aac_encoder_apply_config(encoder, config);
    if (ret != 0) {
        aacEncClose(&encoder->handle);
        free(encoder);
        return NULL;
    }

    ret = aac_encoder_update_info(encoder);
    if (ret != 0) {
        aacEncClose(&encoder->handle);
        free(encoder);
        return NULL;
    }

    encoder->configured = 1;
    return encoder;
}

void aac_encoder_destroy(aac_encoder_t* encoder) {
    if (encoder == NULL) {
        return;
    }

    if (encoder->handle != NULL) {
        aacEncClose(&encoder->handle);
        encoder->handle = NULL;
    }

    memset(encoder, 0, sizeof(*encoder));
    free(encoder);
}

int aac_encoder_get_config(aac_encoder_t* encoder, aac_encoder_config_t* config, aac_encoder_info_t* info) {
    if (encoder == NULL || !encoder->configured) {
        return -1;
    }

    if (config != NULL) {
        *config = encoder->config;
    }

    if (info != NULL) {
        *info = encoder->info;
    }

    return 0;
}

int aac_encoder_encode(aac_encoder_t* encoder, const int16_t* pcm, int pcm_samples, uint8_t* out, int out_size,
                       int* out_bytes, int* consumed_samples) {
    AACENC_ERROR err;
    AACENC_BufDesc in_buf_desc;
    AACENC_BufDesc out_buf_desc;
    AACENC_InArgs in_args;
    AACENC_OutArgs out_args;

    void* in_ptr;
    void* out_ptr;

    INT in_identifier;
    INT out_identifier;

    INT in_size;
    INT out_size_int;

    INT in_element_size;
    INT out_element_size;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }

    if (consumed_samples != NULL) {
        *consumed_samples = 0;
    }

    if (encoder == NULL || !encoder->configured || pcm == NULL || pcm_samples <= 0 || out == NULL || out_size <= 0) {
        return -1;
    }

    /*
     * FDK-AAC has an internal buffer, so feeding less than one full frame is valid.
     * In this camera pipeline, the preferred input size is:
     *   info.input_samples = frame_length * channels
     */
    in_ptr = (void*)pcm;
    out_ptr = (void*)out;

    in_identifier = IN_AUDIO_DATA;
    out_identifier = OUT_BITSTREAM_DATA;

    in_size = (INT)(pcm_samples * (int)sizeof(int16_t));
    out_size_int = (INT)out_size;

    in_element_size = (INT)sizeof(int16_t);
    out_element_size = (INT)sizeof(uint8_t);

    memset(&in_buf_desc, 0, sizeof(in_buf_desc));
    memset(&out_buf_desc, 0, sizeof(out_buf_desc));
    memset(&in_args, 0, sizeof(in_args));
    memset(&out_args, 0, sizeof(out_args));

    in_buf_desc.numBufs = 1;
    in_buf_desc.bufs = &in_ptr;
    in_buf_desc.bufferIdentifiers = &in_identifier;
    in_buf_desc.bufSizes = &in_size;
    in_buf_desc.bufElSizes = &in_element_size;

    out_buf_desc.numBufs = 1;
    out_buf_desc.bufs = &out_ptr;
    out_buf_desc.bufferIdentifiers = &out_identifier;
    out_buf_desc.bufSizes = &out_size_int;
    out_buf_desc.bufElSizes = &out_element_size;

    in_args.numInSamples = (INT)pcm_samples;
    in_args.numAncBytes = 0;

    err = aacEncEncode(encoder->handle, &in_buf_desc, &out_buf_desc, &in_args, &out_args);
    if (err != AACENC_OK) {
        return -2;
    }

    if (out_bytes != NULL) {
        *out_bytes = (int)out_args.numOutBytes;
    }

    if (consumed_samples != NULL) {
        *consumed_samples = (int)out_args.numInSamples;
    }

    /*
     * OK, but not enough PCM has been accumulated to emit an AAC frame yet.
     */
    if (out_args.numOutBytes == 0) {
        return 1;
    }

    return 0;
}

int aac_encoder_flush(aac_encoder_t* encoder, uint8_t* out, int out_size, int* out_bytes) {
    AACENC_ERROR err;
    AACENC_BufDesc out_buf_desc;
    AACENC_InArgs in_args;
    AACENC_OutArgs out_args;

    void* out_ptr;
    INT out_identifier;
    INT out_size_int;
    INT out_element_size;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }

    if (encoder == NULL || !encoder->configured || out == NULL || out_size <= 0) {
        return -1;
    }

    out_ptr = (void*)out;
    out_identifier = OUT_BITSTREAM_DATA;
    out_size_int = (INT)out_size;
    out_element_size = (INT)sizeof(uint8_t);

    memset(&out_buf_desc, 0, sizeof(out_buf_desc));
    memset(&in_args, 0, sizeof(in_args));
    memset(&out_args, 0, sizeof(out_args));

    out_buf_desc.numBufs = 1;
    out_buf_desc.bufs = &out_ptr;
    out_buf_desc.bufferIdentifiers = &out_identifier;
    out_buf_desc.bufSizes = &out_size_int;
    out_buf_desc.bufElSizes = &out_element_size;

    /*
     * numInSamples = -1 requests end-of-stream flushing.
     */
    in_args.numInSamples = -1;
    in_args.numAncBytes = 0;

    err = aacEncEncode(encoder->handle, NULL, &out_buf_desc, &in_args, &out_args);

    if (out_bytes != NULL) {
        *out_bytes = (int)out_args.numOutBytes;
    }

    if (err == AACENC_ENCODE_EOF) {
        return 1;
    }

    if (err != AACENC_OK) {
        return -2;
    }

    return 0;
}
