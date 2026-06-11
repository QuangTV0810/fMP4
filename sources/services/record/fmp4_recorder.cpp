/**
 * @file icam_fmp4_recorder.cpp
 * @brief fMP4 rolling-segment recorder — implementation
 *
 * Depends on:
 *   - libhls/include/hls-fmp4.h  (hls_fmp4_t, hls_fmp4_create/destroy/input …)
 *   - libmov/include/mov-format.h (MOV_OBJECT_*, MOV_AV_FLAG_KEYFREAME)
 *   - platform/utils/icam_utils.h (get_unix_time_ms, ICAM_LOGx)
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "fmp4_recorder.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* libhls / libmov — adjust include paths to match your build system */
#include "hls-fmp4.h"
#include "mov-format.h"

#include "platform/utils/icam_utils.h"

#define TAG               "[FMP4_REC]"

/* Maximum path length for generated file names */
#define FMP4_PATH_MAX     256
#define FMP4_PART_TOLERANCE_MS 100

/* Size of the in-memory buffer used for the fMP4 init segment */
#define FMP4_INIT_SEG_BUF (64 * 1024) /* 64 KB — more than enough for moov */

/* ─── Internal state ───────────────────────────────────────────────────────── */

struct icam_fmp4_recorder {
    /* User config (deep-copied strings) */
    icam_fmp4_recorder_cfg_t cfg;
    char record_path_buf[FMP4_PATH_MAX]; /* backing store for cfg.record_path */
    uint8_t video_extra_buf[1024];       /* backing store for video extra_data */
    uint8_t audio_extra_buf[64];         /* backing store for audio extra_data */

    /* libhls handle — re-created on every new segment file */
    hls_fmp4_t* hls;
    int track_video; /* >=0 when track registered */
    int track_audio; /* >=0 when track registered */

    /* Current open segment file */
    FILE* seg_fp;                 /* NULL when no file is open          */
    char seg_path[FMP4_PATH_MAX]; /* path of the currently-open file    */
    int64_t seg_open_time_ms;     /* wall-clock when file was created   */
    bool seg_init_written;        /* true after init segment flushed    */
    int64_t hls_part_duration_ms; /* effective duration passed to libhls */
    int64_t video_base_pts_ms;    /* first video pts in current file    */
    int64_t video_base_dts_ms;    /* first video dts in current file    */
    int64_t audio_base_pts_ms;    /* first audio pts in current file    */

    /* Mutex — protects all hls_fmp4 calls + file I/O                          */
    pthread_mutex_t lock;
};

/* ─── Helpers ──────────────────────────────────────────────────────────────── */

/** Recursive mkdir for every component in `path`. */
static void _ensure_dirs(const char* path) {
    char tmp[FMP4_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    /* strip trailing slash */
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    /* walk forward, creating each component */
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                ICAM_LOGW(TAG, "mkdir %s: %s", tmp, strerror(errno));
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        ICAM_LOGW(TAG, "mkdir %s: %s", tmp, strerror(errno));
    }
}

static int _build_path(const char* tpl, int64_t now_ms, int64_t segment_duration_ms, char* out, size_t out_sz) {
    time_t start_t;
    time_t stop_t;
    struct tm start_tm;
    struct tm stop_tm;
    char root[FMP4_PATH_MAX];
    const char* fallback_root = "./recordings";
    const char* recordings_pos;

    if (out == NULL || out_sz == 0) {
        return -1;
    }

    if (tpl != NULL && tpl[0] != '\0') {
        recordings_pos = strstr(tpl, "recordings");
        if (recordings_pos != NULL) {
            snprintf(root, sizeof(root), "./%s", recordings_pos);
            if (strchr(root, '%') != NULL) {
                snprintf(root, sizeof(root), "%s", fallback_root);
            }
        } else {
            snprintf(root, sizeof(root), "%s", fallback_root);
        }
    } else {
        snprintf(root, sizeof(root), "%s", fallback_root);
    }

    start_t = (time_t)(now_ms / 1000);
    stop_t = (time_t)((now_ms + segment_duration_ms) / 1000);

    localtime_r(&start_t, &start_tm);
    localtime_r(&stop_t, &stop_tm);

    if (snprintf(out, out_sz,
                 "%s/%04d-%02d-%02d/%02d/%02d.%02d.%02d-%02d.%02d.%02d.mp4",
                 root,
                 start_tm.tm_year + 1900, start_tm.tm_mon + 1, start_tm.tm_mday,
                 start_tm.tm_hour,
                 start_tm.tm_hour, start_tm.tm_min, start_tm.tm_sec,
                 stop_tm.tm_hour, stop_tm.tm_min, stop_tm.tm_sec) >= (int)out_sz) {
        ICAM_LOGE(TAG, "path too long");
        return -1;
    }

    {
        char dir[FMP4_PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", out);
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            _ensure_dirs(dir);
        }
    }

    return 0;
}

/* ─── Segment management ───────────────────────────────────────────────────── */

/** fMP4 part-ready callback: called by hls_fmp4 for each completed fragment */
static int _on_fmp4_segment(void* param, const void* data, size_t bytes, int64_t pts, int64_t dts, int64_t duration) {
    (void)pts;
    (void)dts;
    (void)duration;
    icam_fmp4_recorder_t* rec = (icam_fmp4_recorder_t*)param;

    if (!rec->seg_fp || !data || bytes == 0) {
        return 0;
    }

    if (fwrite(data, 1, bytes, rec->seg_fp) != bytes) {
        ICAM_LOGE(TAG, "fwrite fragment failed: %s", strerror(errno));
        return -1;
    }
    fflush(rec->seg_fp);

    return 0;
}

/** Close the current segment file and tear down hls_fmp4. */
static void _close_segment(icam_fmp4_recorder_t* rec) {
    if (rec->hls) {
        hls_fmp4_destroy(rec->hls);
        rec->hls = NULL;
    }
    if (rec->seg_fp) {
        fclose(rec->seg_fp);
        rec->seg_fp = NULL;
        ICAM_LOGI(TAG, "Closed segment: %s", rec->seg_path);
    }
    rec->seg_path[0] = '\0';
    rec->seg_open_time_ms = 0;
    rec->seg_init_written = false;
    rec->track_video = -1;
    rec->track_audio = -1;
    rec->video_base_pts_ms = -1;
    rec->video_base_dts_ms = -1;
    rec->audio_base_pts_ms = -1;
}

/**
 * Open a new segment file, create hls_fmp4, register video+audio tracks
 * and write the init segment.
 *
 * @param rec      recorder context
 * @param now_ms   current wall-clock time (ms)
 * @return 0 on success, -1 on error
 */
static int _open_segment(icam_fmp4_recorder_t* rec, int64_t now_ms) {
    const icam_fmp4_recorder_cfg_t* cfg = &rec->cfg;

    /* Build file path */
    if (_build_path(cfg->record_path, now_ms, cfg->record_segment_duration_ms, rec->seg_path, sizeof(rec->seg_path)) != 0) {
        return -1;
    }

    rec->seg_fp = fopen(rec->seg_path, "wb");
    if (!rec->seg_fp) {
        ICAM_LOGE(TAG, "fopen '%s' failed: %s", rec->seg_path, strerror(errno));
        rec->seg_path[0] = '\0';
        return -1;
    }

    /* Create hls_fmp4 with the part duration */
    rec->hls = hls_fmp4_create(rec->hls_part_duration_ms, _on_fmp4_segment, rec);
    if (!rec->hls) {
        ICAM_LOGE(TAG, "hls_fmp4_create failed");
        fclose(rec->seg_fp);
        rec->seg_fp = NULL;
        return -1;
    }

    /* Register video track */
    rec->track_video = hls_fmp4_add_video(rec->hls, cfg->video_object_type, cfg->video_width, cfg->video_height,
                                          cfg->video_extra_data, cfg->video_extra_data_size);
    if (rec->track_video < 0) {
        ICAM_LOGE(TAG, "hls_fmp4_add_video failed (ret=%d)", rec->track_video);
        _close_segment(rec);
        return -1;
    }

    /* Register audio track (optional — skip if no audio config) */
    rec->track_audio = -1;
    if (cfg->audio_object_type != 0 && cfg->audio_sample_rate > 0) {
        rec->track_audio =
            hls_fmp4_add_audio(rec->hls, cfg->audio_object_type, cfg->audio_channels > 0 ? cfg->audio_channels : 1,
                               cfg->audio_bits_per_sample > 0 ? cfg->audio_bits_per_sample : 16, cfg->audio_sample_rate,
                               cfg->audio_extra_data, cfg->audio_extra_data_size);
        if (rec->track_audio < 0) {
            ICAM_LOGE(TAG, "hls_fmp4_add_audio failed (ret=%d)", rec->track_audio);
            /* Non-fatal — continue without audio track */
            rec->track_audio = -1;
        }
    }

    /* Write init segment (moov box) at the beginning of the file */
    {
        static uint8_t init_buf[FMP4_INIT_SEG_BUF];
        int n = hls_fmp4_init_segment(rec->hls, init_buf, sizeof(init_buf));
        if (n <= 0) {
            ICAM_LOGE(TAG, "hls_fmp4_init_segment failed (ret=%d)", n);
            _close_segment(rec);
            return -1;
        }
        if (fwrite(init_buf, 1, (size_t)n, rec->seg_fp) != (size_t)n) {
            ICAM_LOGE(TAG, "write init segment failed: %s", strerror(errno));
            _close_segment(rec);
            return -1;
        }
        fflush(rec->seg_fp);
        rec->seg_init_written = true;
    }

    rec->seg_open_time_ms = now_ms;
    rec->video_base_pts_ms = -1;
    rec->video_base_dts_ms = -1;
    rec->audio_base_pts_ms = -1;
    ICAM_LOGI(TAG, "Opened segment: %s (part=%lldms seg=%lldms)", rec->seg_path,
              (long long)rec->hls_part_duration_ms, (long long)cfg->record_segment_duration_ms);
    return 0;
}

/* ─── Public API ───────────────────────────────────────────────────────────── */

icam_fmp4_recorder_t* icam_fmp4_recorder_create(const icam_fmp4_recorder_cfg_t* cfg) {
    if (!cfg) {
        return NULL;
    }

    icam_fmp4_recorder_t* rec = (icam_fmp4_recorder_t*)calloc(1, sizeof(*rec));
    if (!rec) {
        return NULL;
    }

    /* Deep-copy config */
    rec->cfg = *cfg;

    if (cfg->record_path) {
        snprintf(rec->record_path_buf, sizeof(rec->record_path_buf), "%s", cfg->record_path);
        rec->cfg.record_path = rec->record_path_buf;
    }
    if (cfg->video_extra_data && cfg->video_extra_data_size > 0) {
        size_t sz = cfg->video_extra_data_size;
        if (sz > sizeof(rec->video_extra_buf)) {
            sz = sizeof(rec->video_extra_buf);
        }
        memcpy(rec->video_extra_buf, cfg->video_extra_data, sz);
        rec->cfg.video_extra_data = rec->video_extra_buf;
        rec->cfg.video_extra_data_size = sz;
    }
    if (cfg->audio_extra_data && cfg->audio_extra_data_size > 0) {
        size_t sz = cfg->audio_extra_data_size;
        if (sz > sizeof(rec->audio_extra_buf)) {
            sz = sizeof(rec->audio_extra_buf);
        }
        memcpy(rec->audio_extra_buf, cfg->audio_extra_data, sz);
        rec->cfg.audio_extra_data = rec->audio_extra_buf;
        rec->cfg.audio_extra_data_size = sz;
    }

    /* Defaults */
    if (rec->cfg.record_part_duration_ms <= 0) {
        rec->cfg.record_part_duration_ms = 1000; /* 1 s */
    }
    if (rec->cfg.record_segment_duration_ms <= 0) {
        rec->cfg.record_segment_duration_ms = 300000; /* 5 min */
    }
    rec->hls_part_duration_ms = rec->cfg.record_part_duration_ms;
    if (rec->hls_part_duration_ms > FMP4_PART_TOLERANCE_MS) {
        rec->hls_part_duration_ms -= FMP4_PART_TOLERANCE_MS;
    }

    rec->track_video = -1;
    rec->track_audio = -1;
    rec->video_base_pts_ms = -1;
    rec->video_base_dts_ms = -1;
    rec->audio_base_pts_ms = -1;

    pthread_mutex_init(&rec->lock, NULL);

    ICAM_LOGI(TAG, "Created (record=%s path='%s' part=%lldms hls_part=%lldms seg=%lldms)", cfg->record ? "ON" : "OFF",
              cfg->record_path ? cfg->record_path : "(null)", (long long)rec->cfg.record_part_duration_ms,
              (long long)rec->hls_part_duration_ms,
              (long long)rec->cfg.record_segment_duration_ms);
    return rec;
}

void icam_fmp4_recorder_destroy(icam_fmp4_recorder_t* rec) {
    if (!rec) {
        return;
    }
    pthread_mutex_lock(&rec->lock);
    _close_segment(rec);
    pthread_mutex_unlock(&rec->lock);
    pthread_mutex_destroy(&rec->lock);
    free(rec);
    ICAM_LOGI(TAG, "Destroyed");
}

void icam_fmp4_recorder_feed_video(icam_fmp4_recorder_t* rec, const void* data, size_t bytes, int64_t pts_ms,
                                   int64_t dts_ms, bool is_key) {
    if (!rec || !rec->cfg.record || !data || bytes == 0) {
        return;
    }

    pthread_mutex_lock(&rec->lock);

    int64_t now_ms = (int64_t)get_unix_time_ms();

    /* ── Segment rotation ────────────────────────────────────────────── */
    if (rec->seg_fp != NULL) {
        int64_t elapsed = now_ms - rec->seg_open_time_ms;
        if (elapsed >= rec->cfg.record_segment_duration_ms && is_key) {
            ICAM_LOGI(TAG, "Rotating segment after %lldms", (long long)elapsed);
            _close_segment(rec);
        }
    }

    /* ── Open segment on first IDR ───────────────────────────────────── */
    if (rec->seg_fp == NULL) {
        if (!is_key) {
            /* Wait for IDR before opening a new file */
            pthread_mutex_unlock(&rec->lock);
            return;
        }
        if (_open_segment(rec, now_ms) != 0) {
            pthread_mutex_unlock(&rec->lock);
            return;
        }
    }

    /* ── Feed frame to hls_fmp4 ──────────────────────────────────────── */
    int flags = is_key ? MOV_AV_FLAG_KEYFREAME : 0;
    if (rec->video_base_pts_ms < 0) {
        rec->video_base_pts_ms = pts_ms;
    }
    if (rec->video_base_dts_ms < 0) {
        rec->video_base_dts_ms = dts_ms;
    }

    int64_t video_pts_ms = pts_ms - rec->video_base_pts_ms;
    int64_t video_dts_ms = dts_ms - rec->video_base_dts_ms;
    int r = hls_fmp4_input(rec->hls, rec->track_video, data, bytes, video_pts_ms, video_dts_ms, flags);
    if (r != 0) {
        ICAM_LOGE(TAG, "hls_fmp4_input video failed (ret=%d)", r);
    }

    pthread_mutex_unlock(&rec->lock);
}

void icam_fmp4_recorder_feed_audio(icam_fmp4_recorder_t* rec, const void* data, size_t bytes, int64_t pts_ms) {
    if (!rec || !rec->cfg.record || !data || bytes == 0) {
        return;
    }

    pthread_mutex_lock(&rec->lock);

    /* Only write if a segment is open and audio track is registered */
    if (rec->seg_fp == NULL || rec->track_audio < 0) {
        pthread_mutex_unlock(&rec->lock);
        return;
    }

    if (rec->audio_base_pts_ms < 0) {
        rec->audio_base_pts_ms = pts_ms;
    }

    int64_t audio_pts_ms = pts_ms - rec->audio_base_pts_ms;
    int r = hls_fmp4_input(rec->hls, rec->track_audio, data, bytes, audio_pts_ms, audio_pts_ms, 0);
    if (r != 0) {
        ICAM_LOGE(TAG, "hls_fmp4_input audio failed (ret=%d)", r);
    }

    pthread_mutex_unlock(&rec->lock);
}
