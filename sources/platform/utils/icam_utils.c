/**
 * @file utils.c
 * @author QuangTV (tranvietquang2016@gmail.com)
 * @brief
 * @version 0.1
 * @date 2026-03-15
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include "platform/utils/icam_utils.h"

uint64_t get_unix_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL);
}

uint64_t get_unix_time_us(void) {
    struct timeval time;
    gettimeofday(&time, NULL);
    return (uint64_t)time.tv_sec * 1000 * 1000 + time.tv_usec;
}

void file_write(const char* path, const uint8_t* data, size_t len) {
    FILE* f = fopen(path, "ab");
    if (f == NULL) {
        fprintf(stderr, "Failed to open file %s for writing\n", path);
        return;
    }
    fwrite(data, 1, len, f);
    fclose(f);
}

int icam_get_device_udid(char* udid_out, size_t size) {
    if (udid_out == NULL || size == 0) {
        return -1;
    }

    FILE* fp;
    char buffer[128] = {0};
    const char* cmd = "caminfo udid";

    fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("popen failed");
        return -1;
    }

    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        while (len > 0 && (isspace((unsigned char)buffer[len - 1]) || buffer[len - 1] == '#')) {
            buffer[len - 1] = '\0';
            len--;
        }

        strncpy(udid_out, buffer, size - 1);
        udid_out[size - 1] = '\0';
    } else {
        pclose(fp);
        return -1;
    }

    int status = pclose(fp);
    if (status == -1) {
        return -1;
    }

    return 0;
}