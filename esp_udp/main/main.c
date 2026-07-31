/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * Simple Video Server - Main Entry Point
 *
 * This is the application entry point. All business logic is delegated to
 * dedicated components:
 *   - video_server:  HTTP server, MJPEG streaming, camera management
 *   - h264_udp:      H.264 hardware encoding and UDP streaming
 *   - ota_manager:   OTA firmware updates
 */

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "lwip/apps/netbiosns.h"
#include "protocol_examples_common.h"

#include "example_video_common.h"
#include "video_server.h"
#include "h264_udp_stream.h"
#include "ota_manager.h"

static const char *TAG = "app_main";

/* ================================================================
 *  mDNS & NetBIOS 初始化
 * ================================================================ */
static void initialise_mdns(void)
{
    mdns_init();
    mdns_hostname_set(CONFIG_EXAMPLE_MDNS_HOST_NAME);
    mdns_instance_name_set(CONFIG_EXAMPLE_MDNS_INSTANCE);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "esp32"},
        {"path", "/"}
    };

    ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80,
                                      serviceTxtData,
                                      sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
}

/* ================================================================
 *  Application Entry Point
 * ================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  Simple Video Server v2.0");
    ESP_LOGI(TAG, "  Architecture: Component-based (Large-Factory)");
    ESP_LOGI(TAG, "==============================================");

    /* ---- Initialize NVS (Non-Volatile Storage) ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#ifdef CONFIG_EXAMPLE_H264_LOOPBACK_TEST
    /* ---- H.264 Loopback Test Mode (no camera needed) ---- */
    ESP_LOGI(TAG, "Running in H.264 loopback test mode...");
    ESP_ERROR_CHECK(h264_loopback_test_init());
    ESP_LOGI(TAG, "Loopback test finished.");
    return;
#else
    /* ---- Mark firmware as valid (for OTA rollback support) ---- */
    ota_manager_mark_app_valid();

    /* ---- Initialize video subsystem (provides XCLK to cameras) ---- */
    ESP_ERROR_CHECK(example_video_init());

    /* ---- Initialize network stack ---- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ---- mDNS & NetBIOS ---- */
    initialise_mdns();
    netbiosns_init();
    netbiosns_set_name(CONFIG_EXAMPLE_MDNS_HOST_NAME);

    /* ---- Wi-Fi connection (with retry) ---- */
    {
        int wifi_retry_count = 0;
        const int WIFI_MAX_RETRY = CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY;
        esp_err_t wifi_ret = ESP_FAIL;

        while (wifi_retry_count < WIFI_MAX_RETRY) {
            wifi_ret = example_connect();
            if (wifi_ret == ESP_OK) {
                ESP_LOGI(TAG, "Wi-Fi connected successfully");
                break;
            }
            wifi_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi connection failed (attempt %d/%d), retrying in 5s...",
                     wifi_retry_count, WIFI_MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }

        if (wifi_ret != ESP_OK) {
            ESP_LOGE(TAG, "============================================================");
            ESP_LOGE(TAG, "Wi-Fi connection failed after %d retries!", WIFI_MAX_RETRY);
            ESP_LOGE(TAG, "Video server will start in local-only mode.");
            ESP_LOGE(TAG, "============================================================");
        }
    }

    /* ---- Build camera device configuration ---- */
    video_server_camera_config_t camera_configs[5];
    int config_count = 0;

#if EXAMPLE_ENABLE_MIPI_CSI_CAM_SENSOR
    camera_configs[config_count++] = (video_server_camera_config_t){
        .dev_name = ESP_VIDEO_MIPI_CSI_DEVICE_NAME,
    };
#endif
#if EXAMPLE_ENABLE_DVP_CAM_SENSOR
    camera_configs[config_count++] = (video_server_camera_config_t){
        .dev_name = ESP_VIDEO_DVP_DEVICE_NAME,
    };
#endif
#if EXAMPLE_ENABLE_SPI_CAM_0_SENSOR
    camera_configs[config_count++] = (video_server_camera_config_t){
        .dev_name = ESP_VIDEO_SPI_DEVICE_NAME,
    };
#endif
#if EXAMPLE_ENABLE_SPI_CAM_1_SENSOR
    camera_configs[config_count++] = (video_server_camera_config_t){
        .dev_name = ESP_VIDEO_SPI_DEVICE_1_NAME,
    };
#endif
#if EXAMPLE_ENABLE_USB_UVC_CAM_SENSOR
    camera_configs[config_count++] = (video_server_camera_config_t){
        .dev_name = ESP_VIDEO_USB_UVC_DEVICE_NAME(0),
    };
#endif

    assert(config_count > 0);

    /* ---- Start the video server ---- */
    ESP_ERROR_CHECK(video_server_start(camera_configs, config_count));

    ESP_LOGI(TAG, "Camera web server started successfully!");
#endif /* CONFIG_EXAMPLE_H264_LOOPBACK_TEST */
}
