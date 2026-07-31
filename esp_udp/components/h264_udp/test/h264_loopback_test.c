/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * H.264 Loopback Test - Standalone test (no camera needed)
 *
 * NOTE: This is a wrapper that delegates to the original loopback test
 * implementation in the parent project's main/h264_loopback_test.c.
 * For full loopback test functionality, ensure CONFIG_EXAMPLE_H264_LOOPBACK_TEST=y.
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "h264_udp_stream.h"

static const char *TAG = "h264_lbt";

#ifndef CONFIG_EXAMPLE_H264_LOOPBACK_TEST

esp_err_t h264_loopback_test_init(void)
{
    ESP_LOGW(TAG, "H.264 loopback test not enabled in Kconfig.");
    ESP_LOGW(TAG, "Set CONFIG_EXAMPLE_H264_LOOPBACK_TEST=y to enable.");
    return ESP_ERR_NOT_SUPPORTED;
}

#else

/* Include the original loopback test from parent project */
/* The original h264_loopback_test.c will be linked when CONFIG_EXAMPLE_H264_LOOPBACK_TEST=y */

#endif
