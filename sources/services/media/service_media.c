#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "services/media/service_media.h"
#include "services/media/aac_encoder.h"
#include "hal/rts3917n/hal_audio.h"
#include "hal/rts3917n/hal_video.h"

#define MEDIA_PCM_CACHE_MAX_SAMPLES (4U * 1024U)
#define MEDIA_AAC_OUT_MAX_SIZE      (1U * 1024U)

typedef struct {
    aac_encoder_t* encoder;
    aac_encoder_info_t info;
    uint8_t pcm_cache[MEDIA_PCM_CACHE_MAX_SAMPLES];
    size_t pcm_cache_used;
    uint8_t out[MEDIA_AAC_OUT_MAX_SIZE];
    uint64_t pts;
} service_media_aac_t;

struct service_media {
    service_media_opts_t opts;
    hal_video_t* hal_video;
    hal_audio_t* hal_audio;
    service_media_aac_t aac;
    bool running;
};

static void _emit_video(service_media_t* service, const service_media_frame_t* frame, void (*cb)(const service_media_frame_t*, void*))
{
    if (service == NULL || frame == NULL || cb == NULL) {
        return;
    }

    cb(frame, service->opts.ctx);
}

static void _emit_audio(service_media_t* service, const service_media_frame_t* frame, void (*cb)(const service_media_frame_t*, void*))
{
    if (service == NULL || frame == NULL || cb == NULL) {
        return;
    }

    cb(frame, service->opts.ctx);
}

static void _encode_aac(service_media_t* service)
{
    int out_bytes;
    int consumed_samples;
    int ret;
    size_t consumed_bytes;
    service_media_aac_t* aac;

    if (service == NULL) {
        return;
    }

    aac = &service->aac;
    if (aac->encoder == NULL) {
        return;
    }

    while (aac->pcm_cache_used >= (size_t) aac->info.input_samples * sizeof(int16_t)) {
        service_media_frame_t frame;

        out_bytes = 0;
        consumed_samples = 0;
        ret = aac_encoder_encode(aac->encoder, (const int16_t*) aac->pcm_cache, (int) (aac->pcm_cache_used / sizeof(int16_t)),
                                 aac->out, (int) sizeof(aac->out), &out_bytes, &consumed_samples);
        if (ret < 0) {
            return;
        }

        consumed_bytes = (size_t) consumed_samples * sizeof(int16_t);
        if (consumed_bytes == 0U || consumed_bytes > aac->pcm_cache_used) {
            break;
        }

        if (out_bytes > 0) {
            memset(&frame, 0, sizeof(frame));
            
            frame.codec = ICAM_CODEC_AAC;
            frame.pts = aac->pts;
            frame.data = aac->out;
            frame.len = (uint32_t) out_bytes;
            frame.channels = (uint16_t) aac->info.channels;
            frame.sample_rate = (uint32_t) aac->info.sample_rate;

            _emit_audio(service, &frame, service->opts.audio_opts.on_audio_aac);
        }

        memmove(aac->pcm_cache, aac->pcm_cache + consumed_bytes, aac->pcm_cache_used - consumed_bytes);
        aac->pcm_cache_used -= consumed_bytes;
    }

    return;
}

static void _on_video_main(hal_video_frame_t* hal_frame, void* ctx)
{
    service_media_t* service = (service_media_t*) ctx;
    service_media_frame_t frame;

    if (service == NULL || hal_frame == NULL || service->opts.video_opts.on_video_main == NULL) {
        return;
    }

    memset(&frame, 0, sizeof(frame));
    frame.codec = service->opts.video_opts.cfg.main.codec != ICAM_CODEC_UNKNOWN ? service->opts.video_opts.cfg.main.codec : ICAM_CODEC_H264;
    frame.pts = hal_frame->pts;
    frame.data = hal_frame->data;
    frame.len = hal_frame->len;
    frame.flags = hal_frame->is_key ? 1U : 0U;
    _emit_video(service, &frame, service->opts.video_opts.on_video_main);
}

static void _on_video_sub(hal_video_frame_t* hal_frame, void* ctx)
{
    service_media_t* service = (service_media_t*) ctx;
    service_media_frame_t frame;

    if (service == NULL || hal_frame == NULL || service->opts.video_opts.on_video_sub == NULL) {
        return;
    }

    memset(&frame, 0, sizeof(frame));
    frame.codec = service->opts.video_opts.cfg.sub.codec != ICAM_CODEC_UNKNOWN ? service->opts.video_opts.cfg.sub.codec : ICAM_CODEC_H264;
    frame.pts = hal_frame->pts;
    frame.data = hal_frame->data;
    frame.len = hal_frame->len;
    frame.flags = hal_frame->is_key ? 1U : 0U;
    _emit_video(service, &frame, service->opts.video_opts.on_video_sub);
}

static void _on_audio_g711(icam_hal_audio_frame_t* hal_frame, void* ctx)
{
    service_media_t* service = (service_media_t*) ctx;
    service_media_frame_t frame;

    if (service == NULL || hal_frame == NULL || service->opts.audio_opts.on_audio_g711 == NULL) {
        return;
    }

    memset(&frame, 0, sizeof(frame));
    frame.codec = ICAM_CODEC_G711A;
    frame.pts = hal_frame->pts;
    frame.data = hal_frame->data;
    frame.len = hal_frame->len;
    frame.channels = 1U;
    frame.sample_rate = 8000U;
    _emit_audio(service, &frame, service->opts.audio_opts.on_audio_g711);
}

static void _on_audio_pcm(icam_hal_audio_frame_t* hal_frame, void* ctx)
{
    service_media_t* service = (service_media_t*) ctx;
    service_media_frame_t frame;
    size_t free_bytes;
    size_t copy_bytes;
    service_media_aac_t* aac;

    if (service == NULL || hal_frame == NULL) {
        return;
    }

    if (service->opts.audio_opts.on_audio_pcm != NULL) {
        memset(&frame, 0, sizeof(frame));
        frame.codec = ICAM_CODEC_PCM;
        frame.pts = hal_frame->pts;
        frame.data = hal_frame->data;
        frame.len = hal_frame->len;
        // frame.channels = (uint16_t) (service->opts.audio_opts.cfg.channels > 0 ? service->opts.audio_opts.cfg.channels : 1);
        // frame.sample_rate = (uint32_t) (service->opts.audio_opts.cfg.samplerate > 0 ? service->opts.audio_opts.cfg.samplerate : 16000);
        _emit_audio(service, &frame, service->opts.audio_opts.on_audio_pcm);
    }

    aac = &service->aac;
    if (aac->encoder == NULL) {
        return;
    }

    if (hal_frame->len > sizeof(aac->pcm_cache)) {
        aac->pcm_cache_used = 0U;
        return;
    }

    free_bytes = sizeof(aac->pcm_cache) - aac->pcm_cache_used;
    if (hal_frame->len > free_bytes) {
        aac->pcm_cache_used = 0U;
        free_bytes = sizeof(aac->pcm_cache);
    }

    copy_bytes = hal_frame->len <= free_bytes ? hal_frame->len : free_bytes;
    if (hal_frame->len <= free_bytes) {
        copy_bytes = hal_frame->len;    // If there is enough space in the buffer, copy the entire frame.
    } else {
        copy_bytes = free_bytes;        // If there isn't enough space, just copy the remaining blank areas.
    }

    memcpy(aac->pcm_cache + aac->pcm_cache_used, hal_frame->data, copy_bytes);
    aac->pcm_cache_used += copy_bytes;
    aac->pts = hal_frame->pts;
    _encode_aac(service);
}

service_media_t* service_media_create(const service_media_opts_t* cfg)
{
    service_media_t* service;

    if (cfg == NULL) {
        return NULL;
    }

    service = (service_media_t*) calloc(1, sizeof(*service));
    if (service == NULL) {
        return NULL;
    }

    service->opts = *cfg;

    if (cfg->video_opts.cfg.main.enable || cfg->video_opts.cfg.sub.enable) {
        hal_video_cfg_t hal_video_cfg;
        memset(&hal_video_cfg, 0, sizeof(hal_video_cfg));

        if (cfg->video_opts.cfg.main.enable) {
            hal_video_cfg.on_video_main = _on_video_main;
        }

        if (cfg->video_opts.cfg.sub.enable) {
            hal_video_cfg.on_video_sub = _on_video_sub;
        }

        hal_video_cfg.ctx = service;

        service->hal_video = hal_video_create(&hal_video_cfg);
        if (service->hal_video == NULL) {
            free(service);
            return NULL;
        }
    }

    if (cfg->audio_opts.cfg.enable_pcm || cfg->audio_opts.cfg.enable_g711 || cfg->audio_opts.cfg.enable_aac) {
        icam_hal_audio_opts_t hal_audio_cfg;
        memset(&hal_audio_cfg, 0, sizeof(hal_audio_cfg));

        if (cfg->audio_opts.cfg.enable_pcm || cfg->audio_opts.cfg.enable_aac) {
            hal_audio_cfg.on_audio_pcm = _on_audio_pcm;
        }

        if (cfg->audio_opts.cfg.enable_g711) {
            hal_audio_cfg.on_audio_g711 = _on_audio_g711;
        }
        
        hal_audio_cfg.ctx = service;

        service->hal_audio = icam_hal_audio_create(&hal_audio_cfg);
        if (service->hal_audio == NULL) {
            if (service->hal_video != NULL) {
                hal_video_destroy(service->hal_video);
            }
            free(service);
            return NULL;
        }
    }

    if (cfg->audio_opts.cfg.enable_aac) {
        aac_encoder_config_t aac_cfg;

        memset(&aac_cfg, 0, sizeof(aac_cfg));
        if (cfg->audio_opts.cfg.samplerate > 0) {
            aac_cfg.sample_rate = cfg->audio_opts.cfg.samplerate;
        } else {
            aac_cfg.sample_rate = 16000;
        }

        if (cfg->audio_opts.cfg.channels > 0) {
            aac_cfg.channels = cfg->audio_opts.cfg.channels;
        } else {
            aac_cfg.channels = 1;
        }
        if (cfg->audio_opts.cfg.bitrate > 0) {
            aac_cfg.bitrate = cfg->audio_opts.cfg.bitrate;
        } else {
            aac_cfg.bitrate = 48000;
        }

        aac_cfg.afterburner = 0;
        aac_cfg.transport = AAC_ENCODER_TRANSPORT_RAW;

        service->aac.encoder = aac_encoder_create(&aac_cfg);
        if (service->aac.encoder == NULL) {
            service_media_destroy(service);
            return NULL;
        }

        if (aac_encoder_get_config(service->aac.encoder, NULL, &service->aac.info) != 0) {
            service_media_destroy(service);
            return NULL;
        }

    }

    return service;
}

int service_media_start(service_media_t* handle)
{
    int err;

    if (handle == NULL) {
        return ICAM_ERR_INVALID_ARG;
    }

    if (handle->hal_video != NULL) {
        err = hal_video_start(handle->hal_video);
        if (err != ICAM_OK) {
            return ICAM_ERR_SERVICE_VIDEO_ERR;
        }
    }

    if (handle->hal_audio != NULL) {
        err = icam_hal_audio_start(handle->hal_audio);
        if (err != ICAM_OK) {
            if (handle->hal_video != NULL) {
                hal_video_stop(handle->hal_video);
            }
            return ICAM_ERR_SERVICE_AUIDO_ERR;
        }
    }

    handle->running = true;
    return ICAM_OK;
}

int service_media_stop(service_media_t* handle)
{
    if (handle == NULL) {
        return ICAM_ERR_INVALID_ARG;
    }

    handle->running = false;

    if (handle->hal_video != NULL) {
        hal_video_stop(handle->hal_video);
    }

    if (handle->hal_audio != NULL) {
        icam_hal_audio_stop(handle->hal_audio);
    }

    return ICAM_OK;
}

int service_media_process_video(service_media_t* handle)
{
    if (handle == NULL) {
        return ICAM_ERR_INVALID_ARG;
    }

    if (!handle->running || handle->hal_video == NULL) {
        return ICAM_OK;
    }

    return hal_video_process(handle->hal_video);
}

int service_media_process_audio(service_media_t* handle)
{
    if (handle == NULL) {
        return ICAM_ERR_INVALID_ARG;
    }

    if (!handle->running || handle->hal_audio == NULL) {
        return ICAM_OK;
    }

    return icam_hal_audio_process(handle->hal_audio);
}

void service_media_destroy(service_media_t* handle)
{
    if (handle == NULL) {
        return;
    }

    if (handle->hal_video != NULL) {
        hal_video_destroy(handle->hal_video);
        handle->hal_video = NULL;
    }

    if (handle->hal_audio != NULL) {
        icam_hal_audio_destroy(handle->hal_audio);
        handle->hal_audio = NULL;
    }

    if (handle->aac.encoder != NULL) {
        aac_encoder_destroy(handle->aac.encoder);
        handle->aac.encoder = NULL;
    }

    free(handle);
}
