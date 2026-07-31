/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * Video Server - Component orchestrator
 *
 * Coordinates camera initialization, HTTP server startup, MJPEG streaming,
 * and H.264 UDP streaming.
 */

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "video_server.h"
#include "camera_manager.h"
#include "h264_udp_stream.h"

static const char *TAG = "video_server";

/* Maximum cameras supported */
#define MAX_CAMERAS  5

/* Forward declaration */
esp_err_t http_register_handlers(httpd_handle_t server, camera_ctx_t *cameras, int num_cams);

/* ================================================================
 *  Video Server Orchestrator
 * ================================================================ */
esp_err_t video_server_start(const video_server_camera_config_t *configs, int num_cameras)
{
    if (!configs || num_cameras <= 0 || num_cameras > MAX_CAMERAS) {
        ESP_LOGE(TAG, "Invalid camera config: count=%d", num_cameras);
        return ESP_ERR_INVALID_ARG;
    }

    /* Allocate camera contexts */
    camera_ctx_t *cameras = calloc(num_cameras, sizeof(camera_ctx_t));
    if (!cameras) {
        ESP_LOGE(TAG, "Failed to allocate camera contexts");
        return ESP_ERR_NO_MEM;
    }

    /* ---- Phase 1: Initialize all cameras ---- */
    int active_cams = 0;
    for (int i = 0; i < num_cameras; i++) {
        esp_err_t ret = camera_init(&cameras[i], configs[i].dev_name, i);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Camera[%d] (%s) init failed, skipping", i, configs[i].dev_name);
            continue;
        }
        active_cams++;
    }

    if (active_cams == 0) {
        ESP_LOGE(TAG, "No cameras initialized successfully!");
        free(cameras);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "%d/%d cameras initialized", active_cams, num_cameras);

    /* ---- Phase 2: Start HTTP server ---- */
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 16;
    http_cfg.server_port = 80;

    httpd_handle_t server = NULL;
    ESP_GOTO_ON_ERROR(httpd_start(&server, &http_cfg), cleanup, TAG,
                      "Failed to start HTTP server");

    /* Register URI handlers */
    ESP_GOTO_ON_ERROR(http_register_handlers(server, cameras, num_cameras),
                      cleanup_http, TAG, "Failed to register URI handlers");

    ESP_LOGI(TAG, "HTTP server started on port 80");

    /* ---- Phase 3: Start H.264 UDP streaming for each camera ---- */
#ifndef CONFIG_EXAMPLE_H264_LOOPBACK_TEST
    esp_err_t h264_ret = h264_udp_stream_init(cameras, num_cameras);
    if (h264_ret != ESP_OK) {
        ESP_LOGW(TAG, "H.264 UDP streaming init failed (non-fatal)");
    }
#endif

    ESP_LOGI(TAG, "Video server fully started with %d camera(s)", active_cams);
    return ESP_OK;

cleanup_http:
    httpd_stop(server);
cleanup:
    for (int i = 0; i < num_cameras; i++) {
        if (camera_is_valid(&cameras[i])) {
            camera_deinit(&cameras[i]);
        }
    }
    free(cameras);
    return ESP_FAIL;
}
