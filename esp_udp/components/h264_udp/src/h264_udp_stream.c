/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * H.264 UDP Stream - Orchestrates H.264 encoding and UDP streaming per camera
 */

#include <string.h>
#include <errno.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "h264_udp_stream.h"
#include "h264_encoder.h"
#include "h264_protocol.h"
#include "camera_manager.h"

static const char *TAG = "h264_udp";

/* Global UDP socket state */
static int s_udp_socket = -1;
static struct sockaddr_in s_client_addr;
/* ================================================================
 *  UDP Listener Task (Handshake)
 * ================================================================ */
static void h264_udp_listener_task(void *arg)
{
    uint8_t rx_buf[64];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);
    bool waiting_for_ack = false;
    int64_t handshake_start = 0;
    int handshake_retry_count = 0;

    ESP_LOGI(TAG, "UDP listener started, waiting for client HELLO...");

    while (true) {
        /* Wait for client HELLO */
        int len = recvfrom(s_udp_socket, rx_buf, sizeof(rx_buf), MSG_DONTWAIT,
                           (struct sockaddr *)&src_addr, &src_len);

        if (len > 0 && h264_handshake_is_hello(rx_buf, len)) {
            if (!waiting_for_ack) {
                /* New client detected */
                memcpy(&s_client_addr, &src_addr, sizeof(s_client_addr));
                s_client_configured = true;
                waiting_for_ack = true;
                handshake_start = esp_timer_get_time();
                handshake_retry_count = 0;

                ESP_LOGI(TAG, "Client HELLO from %s:%d, sending READY...",
                         inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));

                h264_handshake_send_ready(s_udp_socket, &s_client_addr);
            } else if (h264_handshake_is_ack(rx_buf, len)) {
                /* Got ACK */
                s_handshake_done = true;
                waiting_for_ack = false;
                ESP_LOGI(TAG, "Handshake complete! Client ACK received.");
            }
        }

        /* Check handshake timeout */
        if (waiting_for_ack) {
            int64_t elapsed_ms = (esp_timer_get_time() - handshake_start) / 1000;
            if (h264_handshake_check_timeout(handshake_start, handshake_retry_count)) {
                ESP_LOGW(TAG, "Handshake timeout after %lldms, resetting", elapsed_ms);
                waiting_for_ack = false;
            } else if (h264_handshake_should_retry(handshake_start, handshake_retry_count)) {
                handshake_retry_count++;
                h264_handshake_send_ready(s_udp_socket, &s_client_addr);
                ESP_LOGD(TAG, "Resending READY (retry=%d)", handshake_retry_count);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static bool s_client_configured = false;
static uint16_t s_frame_seq = 0;

/* Handshake state */
static bool s_handshake_done = false;

/* ================================================================
 *  Per-Camera H.264 Encoding + Streaming Task
 * ================================================================ */
static void h264_video_task(void *arg)
{
    camera_ctx_t *cam = (camera_ctx_t *)arg;
    if (!camera_is_valid(cam)) {
        ESP_LOGE(TAG, "Invalid camera context for H.264 task");
        vTaskDelete(NULL);
        return;
    }

    /* Initialize encoder for this camera's resolution */
    h264_encoder_t encoder;
    esp_err_t ret = h264_encoder_init(&encoder, cam->width, cam->height);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "H.264 encoder init failed for cam[%d]", cam->index);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "H.264 video task started for cam[%d] (%dx%d)",
             cam->index, cam->width, cam->height);

    while (true) {
        /* Wait for handshake to complete */
        if (!s_handshake_done || !s_client_configured) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Capture frame from camera */
        uint8_t *frame_data = NULL;
        uint32_t frame_size = 0;
        ret = camera_capture_frame(cam, &frame_data, &frame_size);
        if (ret != ESP_OK || frame_size == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* Encode YUV420 -> H.264 */
        uint8_t *h264_data = NULL;
        uint32_t h264_size = 0;
        ret = h264_encoder_encode(&encoder, frame_data, frame_size,
                                  &h264_data, &h264_size);
        if (ret != ESP_OK || h264_size == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* Send H.264 frame via UDP */
        h264_udp_send_frame(s_udp_socket, &s_client_addr,
                            h264_data, h264_size, &s_frame_seq);

        /* Yield to other tasks */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ================================================================
 *  H.264 UDP Stream Initialization
 * ================================================================ */
esp_err_t h264_udp_stream_init(camera_ctx_t *cameras, int num_cameras)
{
    if (!cameras || num_cameras <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Create UDP socket */
    s_udp_socket = h264_udp_socket_create();
    if (s_udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return ESP_FAIL;
    }

    /* Start handshake listener task */
    BaseType_t task_ret = xTaskCreate(h264_udp_listener_task, "h264_listen",
                                      2048, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UDP listener task");
        h264_udp_socket_close(s_udp_socket);
        s_udp_socket = -1;
        return ESP_FAIL;
    }

    /* Start per-camera encoding + streaming tasks */
    for (int i = 0; i < num_cameras; i++) {
        if (!camera_is_valid(&cameras[i])) continue;

        char task_name[16];
        snprintf(task_name, sizeof(task_name), "h264_cam%d", i);
        task_ret = xTaskCreate(h264_video_task, task_name,
                               8192, (void *)&cameras[i], 5, NULL);
        if (task_ret != pdPASS) {
            ESP_LOGW(TAG, "Failed to create H.264 task for cam[%d]", i);
        } else {
            ESP_LOGI(TAG, "H.264 task created for cam[%d]", i);
        }
    }

    ESP_LOGI(TAG, "H.264 UDP streaming initialized");
    return ESP_OK;
}
