/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * Camera Manager - Internal Header
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of video buffers per camera (from Kconfig) */
#define CAMERA_BUFFER_COUNT  CONFIG_EXAMPLE_CAMERA_VIDEO_BUFFER_NUMBER

/**
 * @brief Camera context structure
 */
typedef struct {
    int         fd;
    uint8_t     index;
    uint8_t    *buffer[CAMERA_BUFFER_COUNT];
    uint32_t    buffer_size;
    uint32_t    width;
    uint32_t    height;
    uint32_t    pixel_format;
    uint32_t    bytesperline;
    uint32_t    frame_rate;
    SemaphoreHandle_t sem;
} camera_ctx_t;

/**
 * @brief Check if camera context is valid
 */
bool camera_is_valid(const camera_ctx_t *cam);

/**
 * @brief Initialize a camera device
 */
esp_err_t camera_init(camera_ctx_t *cam, const char *dev_name, int index);

/**
 * @brief Deinitialize a camera device
 */
esp_err_t camera_deinit(camera_ctx_t *cam);

/**
 * @brief Capture a video frame from camera
 *
 * @param[in]  cam       Camera context
 * @param[out] out_data  Pointer to frame data buffer
 * @param[out] out_size  Size of captured frame in bytes
 * @return ESP_OK on success
 */
esp_err_t camera_capture_frame(camera_ctx_t *cam, uint8_t **out_data, uint32_t *out_size);

#ifdef __cplusplus
}
#endif

