/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * OTA Manager - Public API for Over-The-Air firmware updates
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA update status enumeration
 */
typedef enum {
    OTA_STATUS_IDLE = 0,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_SUCCESS,
    OTA_STATUS_FAILED,
} ota_status_t;

/**
 * @brief Start an OTA firmware update from HTTP URL
 *
 * @param ota_url URL of the firmware binary
 * @return ESP_OK if update started, ESP_ERR_INVALID_STATE if already in progress
 */
esp_err_t ota_manager_begin_update(const char *ota_url);

/**
 * @brief Get current OTA update status and progress
 *
 * @param[out] progress Progress percentage (0-100), can be NULL
 * @param[out] status   Current OTA status, can be NULL
 * @return ESP_OK
 */
esp_err_t ota_manager_get_status(int *progress, ota_status_t *status);

/**
 * @brief Get the label of the currently running partition
 *
 * @return Partition label string (static, do not free)
 */
const char *ota_manager_get_running_label(void);

/**
 * @brief Get the label of the next boot partition
 *
 * @return Partition label string (static, do not free)
 */
const char *ota_manager_get_next_boot_label(void);

/**
 * @brief Mark the current firmware as valid (cancel rollback)
 *
 * @return ESP_OK
 */
esp_err_t ota_manager_mark_app_valid(void);

#ifdef __cplusplus
}
#endif
