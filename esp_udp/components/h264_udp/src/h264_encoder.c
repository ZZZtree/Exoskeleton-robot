/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * H.264 Encoder - V4L2 M2M H.264 hardware encoder interface
 */

#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <errno.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
/* ================================================================
 *  H.264 Encoder Initialization
 * ================================================================ */
esp_err_t h264_encoder_init(h264_encoder_t *enc, uint32_t width, uint32_t height)
{
    if (!enc || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(enc, 0, sizeof(*enc));
    enc->fd = -1;
    enc->width = width;
    enc->height = height;

    /* Open M2M device */
    enc->fd = open(ESP_VIDEO_M2M_DEVICE_NAME, O_RDWR);
    if (enc->fd < 0) {
        ESP_LOGE(TAG, "Failed to open M2M device: %d", errno);
        return ESP_ERR_NOT_FOUND;
    }

    /* Configure OUTPUT (input: YUV420 from camera) */
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_S_FMT, &format), fail, TAG,
                      "Failed to set OUTPUT format");

    /* Request OUTPUT buffers (USERPTR - we provide camera buffers) */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = H264_ENC_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_REQBUFS, &req), fail, TAG,
                      "Failed to request OUTPUT buffers");

    /* Configure CAPTURE (output: H.264 encoded stream) */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_S_FMT, &format), fail, TAG,
                      "Failed to set CAPTURE format");

    /* Request CAPTURE buffer (MMAP) */
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_REQBUFS, &req), fail, TAG,
                      "Failed to request CAPTURE buffer");

    /* Query and mmap CAPTURE buffer */
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_QUERYBUF, &buf), fail, TAG,
                      "Failed to query CAPTURE buffer");

    enc->cap_buf_size = buf.length;
    enc->cap_buffer = (uint8_t *)mmap(NULL, buf.length,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, enc->fd, buf.m.offset);
    if (enc->cap_buffer == MAP_FAILED) {
        ESP_LOGE(TAG, "Failed to mmap CAPTURE buffer");
        goto fail;
    }

    /* Queue CAPTURE buffer */
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_QBUF, &buf), fail, TAG,
                      "Failed to queue CAPTURE buffer");

    /* Configure H.264 encoding parameters */
    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control ctrl[4];
    memset(&ctrls, 0, sizeof(ctrls));
    memset(ctrl, 0, sizeof(ctrl));

    ctrls.ctrl_class = V4L2_CID_CODEC_CLASS;
    ctrls.count = 4;
    ctrls.controls = ctrl;

    ctrl[0].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
    ctrl[0].value = 5;
    ctrl[1].id = V4L2_CID_MPEG_VIDEO_BITRATE;
    ctrl[1].value = 4000000;
    ctrl[2].id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
    ctrl[2].value = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    ctrl[3].id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
    ctrl[3].value = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;

    if (ioctl(enc->fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        ESP_LOGW(TAG, "Failed to set some H.264 params (non-fatal)");
    }

    /* Start streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_STREAMON, &type), fail, TAG,
                      "Failed to start CAPTURE stream");

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ESP_GOTO_ON_ERROR(ioctl(enc->fd, VIDIOC_STREAMON, &type), fail, TAG,
                      "Failed to start OUTPUT stream");

    enc->sem = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "H.264 encoder initialized: %dx%d, bitrate=4Mbps, GOP=5",
             width, height);
    return ESP_OK;

fail:
    if (enc->cap_buffer && enc->cap_buffer != MAP_FAILED) {
        munmap(enc->cap_buffer, enc->cap_buf_size);
        enc->cap_buffer = NULL;
    }
    if (enc->fd >= 0) {
        close(enc->fd);
        enc->fd = -1;
    }
    return ESP_FAIL;
}

#include "esp_check.h"

/* ================================================================
 *  Encode a Frame
 * ================================================================ */
esp_err_t h264_encoder_encode(h264_encoder_t *enc, const uint8_t *yuv_data,
                              uint32_t yuv_size, uint8_t **out_data, uint32_t *out_size)
{
    if (!enc || enc->fd < 0 || !yuv_data || !out_data || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Queue OUTPUT buffer (USERPTR pointing to camera YUV frame) */
    struct v4l2_buffer out_buf;
    memset(&out_buf, 0, sizeof(out_buf));
    out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    out_buf.memory = V4L2_MEMORY_USERPTR;
    out_buf.index = 0;
    out_buf.m.userptr = (unsigned long)yuv_data;
    out_buf.length = yuv_size;
    out_buf.bytesused = yuv_size;

    if (ioctl(enc->fd, VIDIOC_QBUF, &out_buf) != 0) {
        ESP_LOGE(TAG, "OUTPUT QBUF failed: %d", errno);
        return ESP_FAIL;
    }

    /* Dequeue CAPTURE buffer (encoded H.264 data) */
    struct v4l2_buffer cap_buf;
    memset(&cap_buf, 0, sizeof(cap_buf));
    cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cap_buf.memory = V4L2_MEMORY_MMAP;
    cap_buf.index = 0;

    if (ioctl(enc->fd, VIDIOC_DQBUF, &cap_buf) != 0) {
        ESP_LOGE(TAG, "CAPTURE DQBUF failed: %d", errno);
        return ESP_FAIL;
    }

    *out_data = enc->cap_buffer;
    *out_size = cap_buf.bytesused;

    /* Re-queue CAPTURE buffer for next frame */
    if (ioctl(enc->fd, VIDIOC_QBUF, &cap_buf) != 0) {
        ESP_LOGW(TAG, "CAPTURE re-QBUF failed: %d", errno);
    }

    return ESP_OK;
}

/* ================================================================
 *  Deinitialize
 * ================================================================ */
esp_err_t h264_encoder_deinit(h264_encoder_t *enc)
{
    if (!enc || enc->fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(enc->fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(enc->fd, VIDIOC_STREAMOFF, &type);

    if (enc->cap_buffer && enc->cap_buffer != MAP_FAILED) {
        munmap(enc->cap_buffer, enc->cap_buf_size);
        enc->cap_buffer = NULL;
    }

    close(enc->fd);
    enc->fd = -1;

    if (enc->sem) {
        vSemaphoreDelete(enc->sem);
        enc->sem = NULL;
    }

    return ESP_OK;
}

#include "esp_heap_caps.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"
#include "h264_encoder.h"

static const char *TAG = "h264_enc";
