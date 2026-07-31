/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * Camera Manager - V4L2 camera device initialization and buffer management
 */

#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"
/* ================================================================
 *  Camera Buffer Management
 * ================================================================ */
bool camera_is_valid(const camera_ctx_t *cam)
{
    return cam && cam->fd != -1;
}

esp_err_t camera_init(camera_ctx_t *cam, const char *dev_name, int index)
{
    if (!cam || !dev_name) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(cam, 0, sizeof(*cam));
    cam->fd = -1;
    cam->index = index;

    int fd = open(dev_name, O_RDWR);
    ESP_RETURN_ON_FALSE(fd >= 0, ESP_ERR_NOT_FOUND, TAG,
                        "Open video device %s failed", dev_name);

    /* Get native format */
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_GOTO_ON_ERROR(ioctl(fd, VIDIOC_G_FMT, &format), fail_open, TAG,
                      "Failed get fmt from %s", dev_name);

    ESP_LOGI(TAG, "Camera[%d] native: " V4L2_FMT_STR " %" PRIu32 "x%" PRIu32,
             index, V4L2_FMT_STR_ARG(format.fmt.pix.pixelformat),
             format.fmt.pix.width, format.fmt.pix.height);

    /* Try setting YUV420 format for H.264 encoder */
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    if (ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
        ESP_LOGW(TAG, "Camera[%d] no YUV420 support, keeping native", index);
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_G_FMT, &format);
    }

    cam->width = format.fmt.pix.width;
    cam->height = format.fmt.pix.height;
    cam->pixel_format = format.fmt.pix.pixelformat;
    cam->bytesperline = format.fmt.pix.bytesperline;

    /* Set frame rate (30fps) */
    struct v4l2_streamparm sparm;
    memset(&sparm, 0, sizeof(sparm));
    sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sparm.parm.capture.timeperframe.numerator = 1;
    sparm.parm.capture.timeperframe.denominator = 30;
    ioctl(fd, VIDIOC_S_PARM, &sparm);

    /* Request MMAP buffers */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = CAMERA_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_GOTO_ON_ERROR(ioctl(fd, VIDIOC_REQBUFS, &req), fail_open, TAG,
                      "Failed to request buffers for camera[%d]", index);

    /* Map and queue each buffer */
    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ESP_GOTO_ON_ERROR(ioctl(fd, VIDIOC_QUERYBUF, &buf), fail_buffers, TAG,
                          "Failed to query buffer[%d]", i);

        cam->buffer[i] = (uint8_t *)mmap(NULL, buf.length,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED, fd, buf.m.offset);
        if (cam->buffer[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap failed for camera[%d] buf[%d]", index, i);
            goto fail_buffers;
        }
        cam->buffer_size = buf.length;

        ESP_GOTO_ON_ERROR(ioctl(fd, VIDIOC_QBUF, &buf), fail_buffers, TAG,
                          "Failed to queue buffer[%d]", i);
    }

    /* Start streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_GOTO_ON_ERROR(ioctl(fd, VIDIOC_STREAMON, &type), fail_buffers, TAG,
                      "Failed to start streaming on camera[%d]", index);

    cam->fd = fd;
    cam->sem = xSemaphoreCreateBinary();
    if (!cam->sem) {
        ESP_LOGE(TAG, "Failed to create semaphore for camera[%d]", index);
        goto fail_buffers;
    }

    ESP_LOGI(TAG, "Camera[%d] ready: %dx%d fmt=0x%x bufs=%d",
             index, cam->width, cam->height, cam->pixel_format, req.count);
    return ESP_OK;

fail_buffers:
    for (uint32_t i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        if (cam->buffer[i] && cam->buffer[i] != MAP_FAILED) {
            munmap(cam->buffer[i], cam->buffer_size);
            cam->buffer[i] = NULL;
        }
    }
fail_open:
    close(fd);
    return ESP_FAIL;
}

#include "example_video_common.h"
#include "camera_manager.h"

static const char *TAG = "camera_mgr";


esp_err_t camera_deinit(camera_ctx_t *cam)
{
    if (!cam || cam->fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_STREAMOFF, &type);

    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        if (cam->buffer[i]) {
            munmap(cam->buffer[i], cam->buffer_size);
            cam->buffer[i] = NULL;
        }
    }

    close(cam->fd);
    cam->fd = -1;

    if (cam->sem) {
        vSemaphoreDelete(cam->sem);
        cam->sem = NULL;
    }

    return ESP_OK;
}

esp_err_t camera_capture_frame(camera_ctx_t *cam, uint8_t **out_data, uint32_t *out_size)
{
    if (!camera_is_valid(cam) || !out_data || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam->fd, VIDIOC_DQBUF, &buf) != 0) {
        ESP_LOGE(TAG, "Camera[%d] DQBUF failed: %d", cam->index, errno);
        return ESP_FAIL;
    }

    *out_data = cam->buffer[buf.index];
    *out_size = buf.bytesused;

    /* Re-queue immediately */
    if (ioctl(cam->fd, VIDIOC_QBUF, &buf) != 0) {
        ESP_LOGW(TAG, "Camera[%d] QBUF failed: %d", cam->index, errno);
    }

    return ESP_OK;
}
