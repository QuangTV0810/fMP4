#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common_types.h"
#include "osapi.h"
#include "platform/common/icam_def.h"
#include "services/media/service_media.h"
#include "services/record/fmp4_recorder.h"
#include "mov-format.h"

#include "record-tasking.h"

#define RECORD_ENABLE              true
#define RECORD_PATH                "./recordings"
#define RECORD_PART_DURATION_MS    1000
#define RECORD_SEGMENT_DURATION_MS (5 * 60 * 1000)
#define DEBUG_EN                   0

#if DEBUG_EN
#define RECORD_DEBUG(...) OS_printf(__VA_ARGS__)
#else
#define RECORD_DEBUG(...) ((void)0)
#endif

typedef struct {
    service_media_t* media_service;
    icam_fmp4_recorder_t* recorder;
} record_task_ctx_t;

static int _aac_sample_rate_index(int sample_rate)
{
    static const int sample_rates[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000,
        22050, 16000, 12000, 11025, 8000, 7350
    };
    size_t i;

    for (i = 0; i < sizeof(sample_rates) / sizeof(sample_rates[0]); ++i) {
        if (sample_rates[i] == sample_rate) {
            return (int)i;
        }
    }

    return -1;
}

static int _build_aac_asc(uint8_t asc[2], int sample_rate, int channels)
{
    int sample_rate_index;
    const int object_type = 2;

    sample_rate_index = _aac_sample_rate_index(sample_rate);
    if (sample_rate_index < 0 || channels <= 0 || channels > 7) {
        return -1;
    }

    asc[0] = (uint8_t)((object_type << 3) | (sample_rate_index >> 1));
    asc[1] = (uint8_t)(((sample_rate_index & 0x01) << 7) | (channels << 3));
    return 0;
}

static void _record_on_video_main(const service_media_frame_t* frame, void* ctx)
{
    record_task_ctx_t* task_ctx = (record_task_ctx_t*)ctx;
    int64_t pts_ms;

    if (task_ctx == NULL || task_ctx->recorder == NULL || frame == NULL || frame->data == NULL || frame->len == 0U) {
        return;
    }

    pts_ms = (int64_t)(frame->pts / 1000U);
    icam_fmp4_recorder_feed_video(task_ctx->recorder, frame->data, frame->len, pts_ms, pts_ms, (frame->flags & 1U) != 0U);
}

static void _record_on_audio_aac(const service_media_frame_t* frame, void* ctx)
{
    record_task_ctx_t* task_ctx = (record_task_ctx_t*)ctx;
    int64_t pts_ms;

    if (task_ctx == NULL || task_ctx->recorder == NULL || frame == NULL || frame->data == NULL || frame->len == 0U) {
        return;
    }

    pts_ms = (int64_t)(frame->pts / 1000U);
    icam_fmp4_recorder_feed_audio(task_ctx->recorder, frame->data, frame->len, pts_ms);
}

void task_record(void)
{
    int status = 0;
    record_task_ctx_t task_ctx;
    service_media_opts_t media_opts;
    icam_fmp4_recorder_cfg_t recorder_cfg;
    uint8_t audio_asc[2];

    memset(&task_ctx, 0, sizeof(task_ctx));
    memset(&media_opts, 0, sizeof(media_opts));
    memset(&recorder_cfg, 0, sizeof(recorder_cfg));

    if (_build_aac_asc(audio_asc, 8000, 1) != 0) {
        OS_printf("[task] Failed to build AAC ASC\n");
        OS_TaskExit();
        return;
    }

    recorder_cfg.record                     = RECORD_ENABLE;
    recorder_cfg.record_path                = RECORD_PATH;
    recorder_cfg.record_part_duration_ms    = RECORD_PART_DURATION_MS;
    recorder_cfg.record_segment_duration_ms = RECORD_SEGMENT_DURATION_MS;

    recorder_cfg.video_width                = 1920;
    recorder_cfg.video_height               = 1080;
    recorder_cfg.video_object_type          = MOV_OBJECT_H264;

    recorder_cfg.audio_channels             = 1;
    recorder_cfg.audio_bits_per_sample      = 16;
    recorder_cfg.audio_sample_rate          = 8000;
    recorder_cfg.audio_object_type          = MOV_OBJECT_AAC;
    recorder_cfg.audio_extra_data           = audio_asc;
    recorder_cfg.audio_extra_data_size      = sizeof(audio_asc);

    task_ctx.recorder = icam_fmp4_recorder_create(&recorder_cfg);
    if (task_ctx.recorder == NULL) {
        OS_printf("[task] Failed to create fMP4 recorder\n");
        OS_TaskExit();
        return;
    }

    media_opts.video_opts.cfg.main.codec    = ICAM_CODEC_H264;
    media_opts.video_opts.cfg.main.width    = 1920;
    media_opts.video_opts.cfg.main.height   = 1080;
    media_opts.video_opts.cfg.main.fps      = 20;
    media_opts.video_opts.cfg.main.enable   = true;

    media_opts.audio_opts.cfg.codec         = ICAM_CODEC_AAC;
    media_opts.audio_opts.cfg.channels      = 1;
    media_opts.audio_opts.cfg.bitrate       = 48000;
    media_opts.audio_opts.cfg.samplerate    = 8000;
    media_opts.audio_opts.cfg.enable_aac    = true;

    media_opts.video_opts.on_video_main     = _record_on_video_main;
    media_opts.audio_opts.on_audio_aac      = _record_on_audio_aac;
    media_opts.ctx                          = &task_ctx;

    task_ctx.media_service = service_media_create(&media_opts);
    if (task_ctx.media_service == NULL) {
        OS_printf("[task] Failed to create record media service\n");
        icam_fmp4_recorder_destroy(task_ctx.recorder);
        OS_TaskExit();
        return;
    }

    if (service_media_start(task_ctx.media_service) != ICAM_OK) {
        OS_printf("[task] Failed to start record media service\n");
        service_media_destroy(task_ctx.media_service);
        icam_fmp4_recorder_destroy(task_ctx.recorder);
        OS_TaskExit();
        return;
    }

    while (1) {
        status = service_media_process_video(task_ctx.media_service);
        if (status != ICAM_OK) {
            RECORD_DEBUG("[record] Error processing video\n");
        }

        status = service_media_process_audio(task_ctx.media_service);
        if (status != ICAM_OK) {
            RECORD_DEBUG("[record] Error processing audio\n");
        }

        OS_TaskDelay(1);
    }
}
