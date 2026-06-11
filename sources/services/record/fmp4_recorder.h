/**
 * @file icam_fmp4_recorder.h
 * @brief fMP4 rolling-segment recorder (uses libhls / hls-fmp4.h)
 *
 * Concept
 * ───────
 *  ┌──────────────────────────────────────────────────────────┐
 *  │ Segment file  (recordSegmentDuration, e.g. 5 min)        │
 *  │  ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ …             │
 *  │  │ part  │ │ part  │ │ part  │ │ part  │                │
 *  │  │ 1 s   │ │ 1 s   │ │ 1 s   │ │ 1 s   │                │
 *  │  └───────┘ └───────┘ └───────┘ └───────┘                │
 *  └──────────────────────────────────────────────────────────┘
 *
 * - Each "part" is one fMP4 fragment emitted by hls_fmp4 every
 *   `recordPartDuration` milliseconds.
 * - All parts belonging to the same segment are appended to a single
 *   rolling .mp4 file whose name encodes the wall-clock start time.
 * - After `recordSegmentDuration` the current file is closed and a new
 *   one is created (starting at the next IDR frame).
 * - An `init.mp4` (fMP4 moov/init segment) is written once per segment
 *   file so the file is self-contained and seekable.
 *
 * Path template (strftime + custom tokens):
 *   %Y  year, %m month, %d day, %H hour, %M minute, %S second
 *   %f  microseconds (6 digits)
 *   %path  resolved sub-directory part before the filename
 *
 * Example: recordings/%Y-%m-%d/%H/%Y-%m-%d_%H-%M-%S.mp4
 *
 * Thread safety
 * ─────────────
 * icam_fmp4_recorder_feed_video() and icam_fmp4_recorder_feed_audio()
 * are called from two different OSAL tasks and protected internally by
 * a mutex — the caller does NOT need to hold any lock.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Opaque handle ────────────────────────────────────────────────────────── */
typedef struct icam_fmp4_recorder icam_fmp4_recorder_t;

/* ─── Configuration ────────────────────────────────────────────────────────── */
typedef struct {
    /* Master switch.  If false the object is created but feeds are no-ops. */
    bool record;

    /**
     * Directory + filename template (strftime tokens + %f for µs).
     * The recorder creates all intermediate directories automatically.
     * Example: "./recordings/%Y-%m-%d/%H/%Y-%m-%d_%H-%M-%S"
     * The .mp4 extension is appended automatically.
     */
    const char* record_path;

    /**
     * Duration of one fMP4 *fragment* (moof+mdat) in milliseconds.
     * hls_fmp4 emits a new fragment when a video keyframe arrives after
     * this many ms have elapsed since the last cut.
     * Typical: 1000 (1 second).
     */
    int64_t record_part_duration_ms;

    /**
     * Duration of one *segment file* in milliseconds.
     * When the wall-clock elapsed time since the file was opened exceeds
     * this value the recorder closes the file and opens a new one on the
     * next IDR.
     * Typical: 300000 (5 minutes).
     */
    int64_t record_segment_duration_ms;

    /* ── Video ──────────────────────────────────────────────────────────── */
    int video_width;
    int video_height;
    /**
     * MOV_OBJECT_H264 (0x21) or MOV_OBJECT_HEVC / MOV_OBJECT_H265.
     * Include mov-format.h from libmov/include before this header if you
     * need the named constants; otherwise pass the raw value.
     */
    uint8_t video_object_type; /* MOV_OBJECT_H264 = 0x21, HEVC = 0x23 */

    /**
     * SPS+PPS in AVCDecoderConfigurationRecord (avcC) format.
     * For HEVC pass an HEVCDecoderConfigurationRecord.
     * The recorder copies this buffer internally.
     */
    const void* video_extra_data;
    size_t video_extra_data_size;

    /* ── Audio ──────────────────────────────────────────────────────────── */
    int audio_channels;
    int audio_bits_per_sample; /* usually 16 */
    int audio_sample_rate;
    uint8_t audio_object_type; /* MOV_OBJECT_AAC = 0x40 */
    /**
     * AudioSpecificConfig (ASC) for AAC-RAW.
     * Must be set when audio_object_type == MOV_OBJECT_AAC.
     */
    const void* audio_extra_data;
    size_t audio_extra_data_size;
} icam_fmp4_recorder_cfg_t;

/* ─── Lifecycle ────────────────────────────────────────────────────────────── */

/**
 * @brief Allocate and initialise the recorder.
 *
 * Does NOT start any threads — the caller feeds data via
 * icam_fmp4_recorder_feed_video() / icam_fmp4_recorder_feed_audio().
 *
 * @param cfg  Configuration (deep-copied internally).
 * @return Handle, or NULL on allocation failure.
 */
icam_fmp4_recorder_t* icam_fmp4_recorder_create(const icam_fmp4_recorder_cfg_t* cfg);

/**
 * @brief Release all resources.  Safe to call even if create() failed.
 */
void icam_fmp4_recorder_destroy(icam_fmp4_recorder_t* rec);

/* ─── Feed API (called from OSAL tasks) ────────────────────────────────────── */

/**
 * @brief Push one H.264/H.265 video access unit.
 *
 * @param rec      Recorder handle.
 * @param data     NAL units in MP4/Annex-B-start-code-stripped,
 *                 length-prefixed (AVCC) format as expected by libmov.
 * @param bytes    Byte count.
 * @param pts_ms   Presentation timestamp in milliseconds.
 * @param dts_ms   Decode timestamp in milliseconds.
 * @param is_key   true for IDR / keyframe.
 */
void icam_fmp4_recorder_feed_video(icam_fmp4_recorder_t* rec, const void* data, size_t bytes, int64_t pts_ms,
                                   int64_t dts_ms, bool is_key);

/**
 * @brief Push one AAC audio access unit (RAW, no ADTS header).
 *
 * @param rec      Recorder handle.
 * @param data     Raw AAC access unit bytes.
 * @param bytes    Byte count.
 * @param pts_ms   Timestamp in milliseconds.
 */
void icam_fmp4_recorder_feed_audio(icam_fmp4_recorder_t* rec, const void* data, size_t bytes, int64_t pts_ms);

#ifdef __cplusplus
}
#endif
