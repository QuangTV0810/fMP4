#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        ICAM_OK                     = 0,
        ICAM_ERR_INVALID_ARG        = -1,
        ICAM_ERR_NO_MEM             = -2,
        ICAM_ERR_IO                 = -3,
        ICAM_ERR_FAIL               = -4,
        ICAM_ERR_START_THREAD       = -5,
        ICAM_ERR_HAL_SET_CFG        = -6,
        ICAM_ERR_HAL_NO_RESOURCE    = -7,
        ICAM_ERR_SERVICE_VIDEO_ERR  = -8,
        ICAM_ERR_SERVICE_AUIDO_ERR  = -9,
        __ICAM_ERR_MAX,
    } icam_err_t;

#ifdef __cplusplus
}
#endif
