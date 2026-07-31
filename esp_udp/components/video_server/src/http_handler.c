/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * HTTP Handler - REST API endpoints, static file serving, OTA control
 */

#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "camera_manager.h"
#include "ota_manager.h"

static const char *TAG = "http_handler";

/* Embedded gzipped frontend assets (provided by CMake EMBED_TXTFILES) */
extern const uint8_t index_html_gz_start[]      asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]        asm("_binary_index_html_gz_end");
extern const uint8_t loading_jpg_gz_start[]     asm("_binary_loading_jpg_gz_start");
extern const uint8_t loading_jpg_gz_end[]       asm("_binary_loading_jpg_gz_end");
extern const uint8_t favicon_ico_gz_start[]     asm("_binary_favicon_ico_gz_start");
extern const uint8_t favicon_ico_gz_end[]       asm("_binary_favicon_ico_gz_end");
extern const uint8_t assets_index_js_gz_start[] asm("_binary_index_js_gz_start");
extern const uint8_t assets_index_js_gz_end[]   asm("_binary_index_js_gz_end");
extern const uint8_t assets_index_css_gz_start[] asm("_binary_index_css_gz_start");
extern const uint8_t assets_index_css_gz_end[]  asm("_binary_index_css_gz_end");

/* Forward declaration */
extern esp_err_t mjpeg_stream_handler(httpd_req_t *req);

/* ================================================================
 *  Static File Handler
 * ================================================================ */
static esp_err_t static_file_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    if (strcmp(uri, "/") == 0) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)index_html_gz_start,
                               index_html_gz_end - index_html_gz_start);
    } else if (strcmp(uri, "/loading.jpg") == 0) {
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)loading_jpg_gz_start,
                               loading_jpg_gz_end - loading_jpg_gz_start);
    } else if (strcmp(uri, "/favicon.ico") == 0) {
        httpd_resp_set_type(req, "image/x-icon");
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)favicon_ico_gz_start,
                               favicon_ico_gz_end - favicon_ico_gz_start);
    } else if (strcmp(uri, "/assets/index.js") == 0) {
        httpd_resp_set_type(req, "application/javascript");
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)assets_index_js_gz_start,
                               assets_index_js_gz_end - assets_index_js_gz_start);
    } else if (strcmp(uri, "/assets/index.css") == 0) {
        httpd_resp_set_type(req, "text/css");
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)assets_index_css_gz_start,
                               assets_index_css_gz_end - assets_index_css_gz_start);
    }

    ESP_LOGW(TAG, "Static file not found: %s", uri);
    httpd_resp_send_404(req);
    return ESP_FAIL;
}


/* ================================================================
 *  Camera Snapshot Handler (single JPEG capture)
 * ================================================================ */
static esp_err_t snapshot_handler(httpd_req_t *req)
{
    camera_ctx_t *cam = (camera_ctx_t *)req->user_ctx;
    if (!camera_is_valid(cam)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint8_t *frame_data = NULL;
    uint32_t frame_size = 0;
    esp_err_t ret = camera_capture_frame(cam, &frame_data, &frame_size);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, (const char *)frame_data, frame_size);
}

/* ================================================================
 *  OTA Status API Handler
 * ================================================================ */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    ota_status_t status;
    int progress;
    ota_manager_get_status(&progress, &status);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "progress", progress);
    cJSON_AddStringToObject(root, "status",
        status == OTA_STATUS_IDLE        ? "idle" :
        status == OTA_STATUS_DOWNLOADING ? "downloading" :
        status == OTA_STATUS_SUCCESS     ? "success" : "failed");
    cJSON_AddStringToObject(root, "running_partition", ota_manager_get_running_label());
    cJSON_AddStringToObject(root, "next_partition", ota_manager_get_next_boot_label());

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_free(json_str);
    cJSON_Delete(root);
    return ret;
}

/* ================================================================
 *  OTA Trigger API Handler
 * ================================================================ */
static esp_err_t ota_trigger_handler(httpd_req_t *req)
{
    char url_buf[256];
    if (httpd_req_get_url_query_str(req, url_buf, sizeof(url_buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'url' parameter");
        return ESP_FAIL;
    }

    char url_value[256];
    if (httpd_query_key_value(url_buf, "url", url_value, sizeof(url_value)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'url' parameter");
        return ESP_FAIL;
    }

    esp_err_t ret = ota_manager_begin_update(url_value);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA start failed");
        return ESP_FAIL;

/* ================================================================
 *  HTTP Server Registration
 * ================================================================ */

/**
 * @brief Register all HTTP URI handlers on the server
 *
 * @param server    HTTP server handle
 * @param cameras   Array of camera contexts
 * @param num_cams  Number of cameras
 * @return ESP_OK on success
 */
esp_err_t http_register_handlers(httpd_handle_t server, camera_ctx_t *cameras, int num_cams)
{
    /* Static file handler */
    httpd_uri_t uri_static = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = static_file_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_static);
    ESP_LOGI(TAG, "Registered: GET /* (static files)");

    /* MJPEG streams - one endpoint per camera */
    for (int i = 0; i < num_cams; i++) {
        if (!camera_is_valid(&cameras[i])) continue;

        char uri_path[32];
        snprintf(uri_path, sizeof(uri_path), "/stream");

        httpd_uri_t uri_stream = {
            .uri      = "/stream",
            .method   = HTTP_GET,
            .handler  = mjpeg_stream_handler,
            .user_ctx = &cameras[i],
        };
        /* Only first camera on /stream, others on /streamN */
        if (i > 0) {
            snprintf(uri_path, sizeof(uri_path), "/stream%d", i);
            uri_stream.uri = uri_path;
        }

        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_stream));
        ESP_LOGI(TAG, "Registered: GET %s (MJPEG stream cam[%d])", uri_path, i);
    }

    /* Snapshot - first camera only */
    if (num_cams > 0 && camera_is_valid(&cameras[0])) {
        httpd_uri_t uri_snap = {
            .uri      = "/snapshot",
            .method   = HTTP_GET,
            .handler  = snapshot_handler,
            .user_ctx = &cameras[0],
        };
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_snap));
        ESP_LOGI(TAG, "Registered: GET /snapshot");
    }

    /* OTA endpoints */
    httpd_uri_t uri_ota_status = {
        .uri      = "/api/ota/status",
        .method   = HTTP_GET,
        .handler  = ota_status_handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_ota_status));

    httpd_uri_t uri_ota_trigger = {
        .uri      = "/api/ota/trigger",
        .method   = HTTP_POST,
        .handler  = ota_trigger_handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_ota_trigger));
    ESP_LOGI(TAG, "Registered: API endpoints (/api/ota/*)");

    return ESP_OK;
}

    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"result\":\"ok\"}", 13);
}
