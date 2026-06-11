/**
 * @file icam_utils.h
 * @author QuangTV (tranvietquang2016@gmail.com)
 * @brief
 * @version 0.1
 * @date 2026-03-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SAFE_FREE(ptr)                                                                                                 \
    if (ptr != NULL) {                                                                                                 \
        free(ptr);                                                                                                     \
        ptr = NULL;                                                                                                    \
    }

uint64_t get_unix_time_ms(void);
uint64_t get_unix_time_us(void);
void file_write(const char* path, const uint8_t* data, size_t len);

// ===================== Colors =====================
// Define ICAM_LOG_NO_COLOR before including this header to disable ANSI colors.
#ifndef ICAM_LOG_NO_COLOR
#define LOG_COLOR_RED    "\x1b[31m"
#define LOG_COLOR_GREEN  "\x1b[32m"
#define LOG_COLOR_YELLOW "\x1b[33m"
#define LOG_COLOR_DEBUG  "\x1b[35m"
#define LOG_COLOR_RESET  "\x1b[0m"
#else
#define LOG_COLOR_RED    ""
#define LOG_COLOR_GREEN  ""
#define LOG_COLOR_YELLOW ""
#define LOG_COLOR_DEBUG  ""
#define LOG_COLOR_RESET  ""
#endif

#define LOG_COLOR_E         LOG_COLOR_RED    // Error
#define LOG_COLOR_W         LOG_COLOR_YELLOW // Warning
#define LOG_COLOR_I         LOG_COLOR_GREEN  // Info
#define LOG_COLOR_D         LOG_COLOR_DEBUG  // Debug

// ===================== Level mask =====================
// Runtime enable/disable by bitmask
#define ICAM_LOG_MASK_ERROR (1u << 0)
#define ICAM_LOG_MASK_WARN  (1u << 1)
#define ICAM_LOG_MASK_INFO  (1u << 2)
#define ICAM_LOG_MASK_DEBUG (1u << 3)
#define ICAM_LOG_MASK_ALL   (ICAM_LOG_MASK_ERROR | ICAM_LOG_MASK_WARN | ICAM_LOG_MASK_INFO | ICAM_LOG_MASK_DEBUG)

// Default mask (can override before include)
#ifndef ICAM_LOG_DEFAULT_MASK
#define ICAM_LOG_DEFAULT_MASK ICAM_LOG_MASK_ALL
#endif

// Define ICAM_LOG_DEFINE_GLOBALS in EXACTLY ONE .c/.cpp before including this header
// to allocate the global mask storage.
// Example:
//   #define ICAM_LOG_DEFINE_GLOBALS
//   #include "icam_log.h"
#ifdef ICAM_LOG_DEFINE_GLOBALS
uint32_t icam_log_mask = (uint32_t)ICAM_LOG_DEFAULT_MASK;
#else
extern uint32_t icam_log_mask;
#endif

// 0: ERROR, 1: WARN, 2: INFO, 3: DEBUG
#ifndef ICAM_LOG_COMPILE_LEVEL
#define ICAM_LOG_COMPILE_LEVEL 2
#endif

// ===================== Helpers =====================
#ifndef ICAM_LOG_FUNC
#if defined(__GNUC__) || defined(__clang__)
#define ICAM_LOG_FUNC __func__
#else
#define ICAM_LOG_FUNC __FUNCTION__
#endif
#endif

static inline uint32_t icam_log_get_mask(void) {
#if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(&icam_log_mask, __ATOMIC_RELAXED);
#else
    return icam_log_mask;
#endif
}

static inline void icam_log_set_mask(uint32_t mask) {
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(&icam_log_mask, mask, __ATOMIC_RELAXED);
#else
    icam_log_mask = mask;
#endif
}

static inline void icam_log_enable(uint32_t level_mask, int enable) {
    uint32_t m = icam_log_get_mask();
    if (enable) {
        m |= level_mask;
    } else {
        m &= ~level_mask;
    }
    icam_log_set_mask(m);
}

static inline int icam_log_is_enabled(uint32_t level_mask) {
    return (icam_log_get_mask() & level_mask) != 0u;
}

// Timestamp: "YYYY-MM-DD HH:MM:SS.mmm"
static inline const char* icam_log_timestamp(char* out, size_t out_sz) {
    // Prefer clock_gettime; fall back to gettimeofday if needed.
    time_t sec = 0;
    long nsec = 0;

#if defined(CLOCK_REALTIME) && !defined(__UCLIBC__)
    // Many uClibc builds still support clock_gettime, but some older ones may be partial.
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        sec = ts.tv_sec;
        nsec = ts.tv_nsec;
    } else {
        sec = time(NULL);
        nsec = 0;
    }
#elif defined(CLOCK_REALTIME)
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        sec = ts.tv_sec;
        nsec = ts.tv_nsec;
    } else {
        sec = time(NULL);
        nsec = 0;
    }
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    sec = tv.tv_sec;
    nsec = (long)tv.tv_usec * 1000L;
#endif

    struct tm tm_local;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__unix__) || defined(__APPLE__)
    localtime_r(&sec, &tm_local);
#else
    struct tm* p = localtime(&sec); // not thread-safe fallback
    if (p) {
        tm_local = *p;
    }
#endif

    char base[32];
    strftime(base, sizeof(base), "%Y-%m-%d %H:%M:%S", &tm_local);
    long ms = (long)(nsec / 1000000L);
    snprintf(out, out_sz, "%s.%03ld", base, ms);
    return out;
}

static inline void log_print(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// ===================== Output format =====================
#define LOG_FORMAT(letter, format) LOG_COLOR_##letter "%s [" #letter "] %s (%s:%d): " format LOG_COLOR_RESET "\n"

// ===================== Logging macros =====================
#if ICAM_LOG_COMPILE_LEVEL >= 0
#define ICAM_LOGE(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        if (icam_log_is_enabled(ICAM_LOG_MASK_ERROR)) {                                                                \
            char _ts[40];                                                                                              \
            icam_log_timestamp(_ts, sizeof(_ts));                                                                      \
            log_print(LOG_FORMAT(E, fmt), _ts, TAG, ICAM_LOG_FUNC, __LINE__, ##__VA_ARGS__);                           \
        }                                                                                                              \
    } while (0)
#else
#define ICAM_LOGE(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        (void)(TAG);                                                                                                   \
    } while (0)
#endif

#if ICAM_LOG_COMPILE_LEVEL >= 1
#define ICAM_LOGW(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        if (icam_log_is_enabled(ICAM_LOG_MASK_WARN)) {                                                                 \
            char _ts[40];                                                                                              \
            icam_log_timestamp(_ts, sizeof(_ts));                                                                      \
            log_print(LOG_FORMAT(W, fmt), _ts, TAG, ICAM_LOG_FUNC, __LINE__, ##__VA_ARGS__);                           \
        }                                                                                                              \
    } while (0)
#else
#define ICAM_LOGW(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        (void)(TAG);                                                                                                   \
    } while (0)
#endif

#if ICAM_LOG_COMPILE_LEVEL >= 2
#define ICAM_LOGI(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        if (icam_log_is_enabled(ICAM_LOG_MASK_INFO)) {                                                                 \
            char _ts[40];                                                                                              \
            icam_log_timestamp(_ts, sizeof(_ts));                                                                      \
            log_print(LOG_FORMAT(I, fmt), _ts, TAG, ICAM_LOG_FUNC, __LINE__, ##__VA_ARGS__);                           \
        }                                                                                                              \
    } while (0)
#else
#define ICAM_LOGI(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        (void)(TAG);                                                                                                   \
    } while (0)
#endif

#if ICAM_LOG_COMPILE_LEVEL >= 3
#define ICAM_LOGD(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        if (icam_log_is_enabled(ICAM_LOG_MASK_DEBUG)) {                                                                \
            char _ts[40];                                                                                              \
            icam_log_timestamp(_ts, sizeof(_ts));                                                                      \
            log_print(LOG_FORMAT(D, fmt), _ts, TAG, ICAM_LOG_FUNC, __LINE__, ##__VA_ARGS__);                           \
        }                                                                                                              \
    } while (0)
#else
#define ICAM_LOGD(TAG, fmt, ...)                                                                                       \
    do {                                                                                                               \
        (void)(TAG);                                                                                                   \
    } while (0)
#endif

int icam_get_device_udid(char* udid_out, size_t size);

#ifdef __cplusplus
}
#endif
