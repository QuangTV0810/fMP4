
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct service_media service_media_t;

typedef enum {
    ICAM_CODEC_UNKNOWN = 0,

    // Audio
    ICAM_CODEC_PCM,
    ICAM_CODEC_G711A,
    ICAM_CODEC_G711U,
    ICAM_CODEC_AAC,
    ICAM_CODEC_OPUS,

    // Video
    ICAM_CODEC_H264,
    ICAM_CODEC_H265,
} service_media_codec_t;

typedef struct {
    service_media_codec_t codec;
    uint64_t pts;
    const uint8_t* data;
    uint32_t len;
    uint32_t flags;
    uint16_t channels;
    uint32_t sample_rate;
} service_media_frame_t;

typedef struct {
    service_media_codec_t codec;
    int channels;
    int bitrate;
    int samplerate;
    bool enable_pcm;
    bool enable_g711;
    bool enable_aac;
} service_audio_cfg_t;

typedef struct {
    service_media_codec_t codec;
    int bitrate;
    int gop;
    int width;
    int height;
    int fps;
    bool enable;
} service_video_stream_cfg_t;

typedef struct {
    service_video_stream_cfg_t main;
    service_video_stream_cfg_t sub;
} service_video_cfg_t;

typedef struct {
    service_video_cfg_t cfg;
    void (*on_video_main)(const service_media_frame_t* frame, void* ctx);
    void (*on_video_sub)(const service_media_frame_t* frame, void* ctx);
} service_video_opts_t;

typedef struct {
    service_audio_cfg_t cfg;
    void (*on_audio_pcm)(const service_media_frame_t* frame, void* ctx);
    void (*on_audio_g711)(const service_media_frame_t* frame, void* ctx);
    void (*on_audio_aac)(const service_media_frame_t* frame, void* ctx);
} service_audio_opts_t;

typedef struct {
    service_video_opts_t video_opts;
    service_audio_opts_t audio_opts;
    void* ctx;
} service_media_opts_t;

service_media_t* service_media_create(const service_media_opts_t* cfg);
int service_media_start(service_media_t* handle);
int service_media_stop(service_media_t* handle);
int service_media_process_video(service_media_t* handle);
int service_media_process_audio(service_media_t* handle);
void service_media_destroy(service_media_t* handle);

#ifdef __cplusplus
}
#endif
