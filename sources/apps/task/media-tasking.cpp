#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "common_types.h"
#include "osapi.h"
#include "platform/common/icam_def.h"
#include "services/media/service_media.h"
#define ICAM_LOG_DEFINE_GLOBALS
#include "platform/utils/icam_utils.h"
#include "media-tasking.h"

#define DEBUG_EN                   0

#if DEBUG_EN
#define MEDIA_DEBUG(...) OS_printf(__VA_ARGS__)
#else
#define MEDIA_DEBUG(...) ((void)0)
#endif

typedef struct {
    service_media_t* media_service;
} media_task_ctx_t;

static void _on_video_main(const service_media_frame_t* frame, void* ctx)
{
    media_task_ctx_t* task_ctx = (media_task_ctx_t*) ctx;

    if (task_ctx == NULL || frame == NULL) {
        return;
    }

    MEDIA_DEBUG("[media][video][1]\t pts=%llu\t len=%u\t flags=0x%x\n", (unsigned long long) frame->pts, frame->len, frame->flags);
}

static void _on_video_sub(const service_media_frame_t* frame, void* ctx)
{
    media_task_ctx_t* task_ctx = (media_task_ctx_t*) ctx;

    if (task_ctx == NULL || frame == NULL) {
        return;
    }

    MEDIA_DEBUG("[media][video][2]\t pts=%llu\t len=%u\t flags=0x%x\n", (unsigned long long) frame->pts, frame->len, frame->flags);
}

static void _on_audio_pcm(const service_media_frame_t* frame, void* ctx)
{
    media_task_ctx_t* task_ctx = (media_task_ctx_t*) ctx;

    if (task_ctx == NULL || frame == NULL) {
        return;
    }

    MEDIA_DEBUG("[media][audio][pcm]\t pts=%llu\t len=%u\t rate=%u\t ch=%u\n", (unsigned long long) frame->pts, frame->len, frame->sample_rate, frame->channels);
}

static void _on_audio_g711(const service_media_frame_t* frame, void* ctx)
{
    media_task_ctx_t* task_ctx = (media_task_ctx_t*) ctx;

    if (task_ctx == NULL || frame == NULL) {
        return;
    }

    MEDIA_DEBUG("[media][audio][g711]\t pts=%llu\t len=%u\n", (unsigned long long) frame->pts, frame->len);
}

static void _on_audio_aac(const service_media_frame_t* frame, void* ctx)
{
    media_task_ctx_t* task_ctx = (media_task_ctx_t*) ctx;

    if (task_ctx == NULL || frame == NULL) {
        return;
    }

    MEDIA_DEBUG("[media][audio][aac]\t pts=%llu\t len=%u\t rate=%u\t ch=%u\n", (unsigned long long) frame->pts, frame->len, frame->sample_rate, frame->channels);
}

void task_media(void)
{
    int status = 0;
    media_task_ctx_t task_ctx;
    service_media_opts_t media_opts;

    memset(&task_ctx, 0, sizeof(task_ctx));
    memset(&media_opts, 0, sizeof(media_opts));

    media_opts.video_opts.cfg.main.codec    = ICAM_CODEC_H264;
    media_opts.video_opts.cfg.main.width    = 1920;
    media_opts.video_opts.cfg.main.height   = 1080;
    media_opts.video_opts.cfg.main.fps      = 20;
    media_opts.video_opts.cfg.main.enable   = true;

    media_opts.video_opts.cfg.sub.codec     = ICAM_CODEC_H264;
    media_opts.video_opts.cfg.sub.width     = 640;
    media_opts.video_opts.cfg.sub.height    = 360;
    media_opts.video_opts.cfg.sub.fps       = 10;
    media_opts.video_opts.cfg.sub.enable    = true;

    media_opts.audio_opts.cfg.codec         = ICAM_CODEC_PCM;
    media_opts.audio_opts.cfg.channels      = 1;
    media_opts.audio_opts.cfg.bitrate       = 48000;
    media_opts.audio_opts.cfg.samplerate    = 16000;
    media_opts.audio_opts.cfg.enable_pcm    = true;
    media_opts.audio_opts.cfg.enable_g711   = true;
    media_opts.audio_opts.cfg.enable_aac    = true;

    media_opts.video_opts.on_video_main     = _on_video_main;
    media_opts.video_opts.on_video_sub      = _on_video_sub;
    media_opts.audio_opts.on_audio_pcm      = _on_audio_pcm;
    media_opts.audio_opts.on_audio_g711     = _on_audio_g711;
    media_opts.audio_opts.on_audio_aac      = _on_audio_aac;
    media_opts.ctx = &task_ctx;

    task_ctx.media_service = service_media_create(&media_opts);
    if (task_ctx.media_service == NULL) {
        OS_printf("[startup] Failed to create service_media\n");
        OS_TaskExit();
        return;
    }

    if (service_media_start(task_ctx.media_service) != ICAM_OK) {
        OS_printf("[startup] Failed to start service_media\n");
        service_media_destroy(task_ctx.media_service);
        OS_TaskExit();
        return;
    }

    while (1) {
        status = service_media_process_video(task_ctx.media_service);
        if (status != ICAM_OK) {
            MEDIA_DEBUG("[media] Error processing video\n");
        }
        status = service_media_process_audio(task_ctx.media_service);
        if (status != ICAM_OK) {
            MEDIA_DEBUG("[media] Error processing audio\n");
        }
        OS_TaskDelay(1);
    }
}
