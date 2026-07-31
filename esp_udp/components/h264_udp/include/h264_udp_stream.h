/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * H.264 UDP Streaming - Public API
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct camera_ctx;
typedef struct camera_ctx camera_ctx_t;

/**
 * @brief Initialize H.264 UDP streaming for all cameras
 *
 * Creates a UDP socket on port 1235, initializes H.264 hardware encoder (M2M),
 * and launches per-camera encoding + streaming tasks.
 *
 * @param[in] cameras    Array of camera contexts
 * @param[in] num_cameras Number of cameras
 * @return ESP_OK on success
 */
esp_err_t h264_udp_stream_init(camera_ctx_t *cameras, int num_cameras);

/**
 * @brief H.264 loopback test (standalone, no camera needed)
 *
 * Generates synthetic YUV420 test frames and sends via UDP.
 * Controlled by CONFIG_EXAMPLE_H264_LOOPBACK_TEST.
 *
 * @return ESP_OK on success
 */
esp_err_t h264_loopback_test_init(void);

#ifdef __cplusplus
}
#endif
