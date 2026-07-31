/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * H.264 Encoder - Internal header
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define H264_ENC_BUFFER_COUNT  4       /* OUTPUT buffer count */
#define H264_ENC_CAP_BUF_SIZE  (2 * 1024 * 1024)  /* CAPTURE buffer size */

/**
 * @brief H.264 encoder context
 */
typedef struct {
    int         fd;             /* M2M device file descriptor */
    uint32_t    width;
    uint32_t    height;
    uint8_t    *cap_buffer;     /* MMAP'd CAPTURE buffer */
    uint32_t    cap_buf_size;
    SemaphoreHandle_t sem;
} h264_encoder_t;

/**
 * @brief Initialize H.264 M2M hardware encoder
 */
esp_err_t h264_encoder_init(h264_encoder_t *enc, uint32_t width, uint32_t height);

/**
 * @brief Encode a YUV420 frame to H.264
 *
 * @param[in]  enc       Encoder context
 * @param[in]  yuv_data  Input YUV420 frame data
 * @param[in]  yuv_size  Input frame size
 * @param[out] out_data  Output H.264 encoded data pointer
 * @param[out] out_size  Output data size
 * @return ESP_OK on success
 */
esp_err_t h264_encoder_encode(h264_encoder_t *enc, const uint8_t *yuv_data,
                              uint32_t yuv_size, uint8_t **out_data, uint32_t *out_size);

/**
 * @brief Deinitialize encoder
 */
esp_err_t h264_encoder_deinit(h264_encoder_t *enc);

#ifdef __cplusplus
}
#endif
