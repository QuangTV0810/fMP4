#include <atomic>
#include <cstdlib>

#include "hal/rts3917n/record/icam_hal_record.h"
#include "icam_record.h"

struct icam_record {
    icam_hal_record_t* hal;
    std::atomic<bool> running;
};

extern "C" {

icam_record_t* icam_record_create(const icam_record_opts_t* opts) {
    icam_record_t* service = (icam_record_t*)calloc(1, sizeof(*service));
    if (service == NULL) {
        return NULL;
    }

    icam_hal_record_cfg_t cfg = {};
    if (opts != NULL) {
        cfg.record_root = opts->record_root;
        cfg.audio_ringbuffer_path = opts->audio_ringbuffer_path;
        cfg.camera_id = opts->camera_id;
        cfg.segment_duration_s = opts->segment_duration_s;
        cfg.audio_channel = opts->audio_channel;
        cfg.audio_depth = opts->audio_depth;
        cfg.video_channel = opts->video_channel;
        cfg.video_profile = opts->video_profile;
        cfg.video_fps = opts->video_fps;
        cfg.video_width = opts->video_width;
        cfg.video_height = opts->video_height;
        cfg.audio_sample_rate = opts->audio_sample_rate;
        cfg.audio_channels = opts->audio_channels;
        cfg.video_codec = opts->video_codec;
        cfg.audio_codec = opts->audio_codec;
    }

    service->hal = icam_hal_record_create(&cfg);
    if (service->hal == NULL) {
        free(service);
        return NULL;
    }

    service->running.store(false);
    return service;
}

icam_err_t icam_record_start(icam_record_t* handle) {
    if (handle == NULL || handle->hal == NULL) {
        return ICAM_ERR_INVALID_ARG;
    }

    icam_err_t err = icam_hal_record_start(handle->hal);
    if (err == ICAM_OK) {
        handle->running.store(true);
    }

    return err;
}

icam_err_t icam_record_stop(icam_record_t* handle) {
    if (handle == NULL || handle->hal == NULL) {
        return ICAM_ERR_INVALID_ARG;
    }

    icam_err_t err = icam_hal_record_stop(handle->hal);
    handle->running.store(false);
    return err;
}

void icam_record_destroy(icam_record_t* handle) {
    if (handle == NULL) {
        return;
    }

    if (handle->hal != NULL) {
        (void)icam_hal_record_destroy(handle->hal);
        handle->hal = NULL;
    }
    free(handle);
}
}
