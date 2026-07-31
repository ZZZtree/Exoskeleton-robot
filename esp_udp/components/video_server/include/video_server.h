/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * Video Server - Public API
 *
 * Provides HTTP-based video streaming server with MJPEG and H.264 UDP support.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Camera device configuration descriptor
 */
typedef struct {
    const char *dev_name;       /*!< V4L2 device name (e.g. /dev/video0) */
    uint32_t    buffer_count;   /*!< Number of video buffers (0 = use default) */
} video_server_camera_config_t;

/**
 * @brief Start the video streaming server
 *
 * Initializes all camera devices, starts the HTTP server with MJPEG streaming
 * endpoints, and launches H.264 UDP streaming tasks for each camera.
 *
 * @param[in] configs    Array of camera device configurations
 * @param[in] num_cameras Number of camera configurations
 * @return
 *     - ESP_OK: Server started successfully
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_FAIL: Initialization failed
 */
esp_err_t video_server_start(const video_server_camera_config_t *configs, int num_cameras);

#ifdef __cplusplus
}
#endif
