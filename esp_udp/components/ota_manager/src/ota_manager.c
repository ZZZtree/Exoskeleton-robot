/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * OTA Manager - Firmware update via HTTP
 */

#include <string.h>
#include <inttypes.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "ota_manager.h"

static const char *TAG = "ota_mgr";
/* OTA control structure */
typedef struct {
    char url[OTA_URL_SIZE];
    ota_status_t status;
    int progress;
    esp_err_t last_error;
} ota_control_t;

static ota_control_t s_ota = {
    .url = {0},
    .status = OTA_STATUS_IDLE,
    .progress = 0,
    .last_error = ESP_OK,
};

/* Partition label caches */
static char s_running_label[32] = {0};
static char s_next_label[32] = {0};

/* ================================================================
 *  OTA Update Task
 * ================================================================ */
static void ota_update_task(void *arg)
{
    esp_err_t ret;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = NULL;
    char ota_buf[OTA_BUFF_SIZE + 1] = {0};
    int content_length = -1;
    int total_read = 0;

    /* Find next OTA partition */
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        s_ota.status = OTA_STATUS_FAILED;
        s_ota.last_error = ESP_ERR_NOT_FOUND;
        goto ota_end;
    }
    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%" PRIx32,
             update_partition->label, update_partition->address);

    /* Initialize HTTP client */
    esp_http_client_config_t cfg = {
        .url = s_ota.url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        s_ota.status = OTA_STATUS_FAILED;
        goto ota_end;
    }

    ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP: %s", esp_err_to_name(ret));
        esp_http_client_cleanup(client);
        s_ota.status = OTA_STATUS_FAILED;
        goto ota_end;
    }

    content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Firmware size: %d bytes", content_length);

    ret = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_ota.status = OTA_STATUS_FAILED;
        goto ota_end;
    }

    /* Download and write loop */
    while (true) {
        int data_read = esp_http_client_read(client, ota_buf, OTA_BUFF_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            s_ota.status = OTA_STATUS_FAILED;
            break;
        } else if (data_read > 0) {
            ret = esp_ota_write(ota_handle, ota_buf, data_read);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(ret));
                s_ota.status = OTA_STATUS_FAILED;
                break;
            }
            total_read += data_read;
            if (content_length > 0) {
                s_ota.progress = (total_read * 100) / content_length;
            }
        } else {
            break; /* EOF */
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (s_ota.status != OTA_STATUS_FAILED) {
        ESP_LOGI(TAG, "OTA download complete: %d bytes", total_read);

        ret = esp_ota_end(ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
            s_ota.status = OTA_STATUS_FAILED;
            goto ota_end;
        }

        ret = esp_ota_set_boot_partition(update_partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(ret));
            s_ota.status = OTA_STATUS_FAILED;
            goto ota_end;
        }

        s_ota.status = OTA_STATUS_SUCCESS;
        ESP_LOGI(TAG, "OTA successful! Rebooting in 3s...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

ota_end:
    if (ota_handle && s_ota.status != OTA_STATUS_SUCCESS) {
        esp_ota_abort(ota_handle);
    }
    if (s_ota.status != OTA_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "OTA update failed");
    }
    vTaskDelete(NULL);
}


#define OTA_URL_SIZE       256
#define OTA_BUFF_SIZE      1024
#define OTA_TASK_STACK     8192
#define OTA_TASK_PRIORITY  5

/* ================================================================
 *  Public API
 * ================================================================ */
esp_err_t ota_manager_begin_update(const char *ota_url)
{
    if (s_ota.status == OTA_STATUS_DOWNLOADING) {
        ESP_LOGE(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    strlcpy(s_ota.url, ota_url, sizeof(s_ota.url));
    s_ota.status = OTA_STATUS_DOWNLOADING;
    s_ota.progress = 0;
    s_ota.last_error = ESP_OK;

    BaseType_t ret = xTaskCreate(ota_update_task, "ota_update",
                                  OTA_TASK_STACK, NULL, OTA_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        s_ota.status = OTA_STATUS_IDLE;
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA started: %s", ota_url);
    return ESP_OK;
}

esp_err_t ota_manager_get_status(int *progress, ota_status_t *status)
{
    if (progress) *progress = s_ota.progress;
    if (status)   *status = s_ota.status;
    return ESP_OK;
}

const char *ota_manager_get_running_label(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (p) {
        strlcpy(s_running_label, p->label, sizeof(s_running_label));
        return s_running_label;
    }
    return "unknown";
}

const char *ota_manager_get_next_boot_label(void)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if (p) {
        strlcpy(s_next_label, p->label, sizeof(s_next_label));
        return s_next_label;
    }
    return "none";
}

esp_err_t ota_manager_mark_app_valid(void)
{
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(ret));
    }
    return ret;
}
