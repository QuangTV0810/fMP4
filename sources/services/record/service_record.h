#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "platform/common/icam_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct icam_record icam_record_t;

typedef struct {
    const char* record_root;
    const char* record_db_path;
    const char* audio_ringbuffer_path;
    const char* camera_id;
    uint32_t segment_duration_s;
    int audio_channel;
    int audio_depth;
    int video_channel;
    int video_profile;
    int video_fps;
    int video_width;
    int video_height;
    int audio_sample_rate;
    int audio_channels;
    const char* video_codec;
    const char* audio_codec;
} icam_record_opts_t;

icam_record_t* icam_record_create(const icam_record_opts_t* opts);
icam_err_t icam_record_start(icam_record_t* handle);
icam_err_t icam_record_stop(icam_record_t* handle);
void icam_record_destroy(icam_record_t* handle);

#ifdef __cplusplus
}
#endif
