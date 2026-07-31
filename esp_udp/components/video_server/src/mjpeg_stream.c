/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * MJPEG Stream - HTTP multipart MJPEG streaming handler
 */

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "camera_manager.h"

#define MJPEG_BOUNDARY  CONFIG_EXAMPLE_HTTP_PART_BOUNDARY
#define MJPEG_PART_HDR  "--" MJPEG_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

static const char *TAG = "mjpeg_stream";

/**
 * @brief MJPEG stream HTTP handler
 *
 * Sends a continuous multipart/x-mixed-replace stream of JPEG frames.
 * Client disconnects by closing the TCP connection.
 */
esp_err_t mjpeg_stream_handler(httpd_req_t *req)
{
    camera_ctx_t *cam = (camera_ctx_t *)req->user_ctx;
    if (!camera_is_valid(cam)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /* Set response headers for MJPEG streaming */
    esp_err_t ret = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=" MJPEG_BOUNDARY);
    if (ret != ESP_OK) {
        return ret;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    ESP_LOGI(TAG, "MJPEG stream started for camera[%d]", cam->index);

    char part_buf[128];
    uint8_t *frame_data = NULL;
    uint32_t frame_size = 0;

    while (true) {
        /* Capture a frame */
        ret = camera_capture_frame(cam, &frame_data, &frame_size);
        if (ret != ESP_OK || frame_size == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* Send multipart boundary header */
        int hdr_len = snprintf(part_buf, sizeof(part_buf), MJPEG_PART_HDR, (unsigned int)frame_size);
        if (httpd_resp_send_chunk(req, part_buf, hdr_len) != ESP_OK) {
            ESP_LOGW(TAG, "Client disconnected from MJPEG stream");
            break;
        }

        /* Send JPEG frame data */
        if (httpd_resp_send_chunk(req, (const char *)frame_data, frame_size) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send frame, client likely disconnected");
            break;
        }

        /* Send boundary terminator */
        if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
            break;
        }

        /* Small yield to prevent task starvation */
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGI(TAG, "MJPEG stream ended for camera[%d]", cam->index);
    return ESP_OK;
}
