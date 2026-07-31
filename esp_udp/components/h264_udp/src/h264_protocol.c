/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * H.264 Protocol - UDP packet framing, fragmentation, and handshake
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "h264_protocol.h"

static const char *TAG = "h264_proto";

/* ================================================================
 *  Protocol Constants
 * ================================================================ */
#define H264_UDP_PORT           1235
#define H264_UDP_MTU            1460
#define H264_UDP_HEADER_SIZE    8
#define H264_UDP_MAX_PAYLOAD    (H264_UDP_MTU - H264_UDP_HEADER_SIZE)

/* Fragment flags */
#define H264_FLAG_FIRST         0x00
#define H264_FLAG_MIDDLE        0x01
#define H264_FLAG_LAST          0x02
#define H264_FLAG_SINGLE        0x03

/* Packet types */
#define H264_PKT_TYPE_VIDEO     0x00
#define H264_PKT_TYPE_SPSPPS    0x01
#define H264_PKT_TYPE_READY     0xFE
#define H264_PKT_TYPE_ACK       0xFD

/* Handshake timing */
#define H264_HANDSHAKE_TIMEOUT_MS   5000
#define H264_HANDSHAKE_RETRY_MS     500

/* ================================================================
 *  UDP Socket Management
 * ================================================================ */
int h264_udp_socket_create(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return -1;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(H264_UDP_PORT);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind UDP port %d", H264_UDP_PORT);
        close(sock);
        return -1;
    }

    /* Enlarge send buffer (8MB) for burst tolerance */
    int sndbuf = 8 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    ESP_LOGI(TAG, "UDP socket created on port %d (sndbuf=%dMB)", H264_UDP_PORT, sndbuf / (1024*1024));
    return sock;
}

void h264_udp_socket_close(int sock)
{
    if (sock >= 0) {
        close(sock);
    }
}

/* ================================================================
 *  Packet Fragmentation & Send
 * ================================================================ */
int h264_udp_send_frame(int sock, const struct sockaddr_in *dst,
                        const uint8_t *data, uint32_t size, uint16_t *seq)
{
    if (sock < 0 || !dst || !data || size == 0) {
        return -1;
    }

    uint8_t header[H264_UDP_HEADER_SIZE];
    uint16_t frame_seq = (*seq)++;
    uint32_t offset = 0;

    if (size <= H264_UDP_MAX_PAYLOAD) {
        /* ---- Single-packet mode ---- */
        header[0] = (frame_seq >> 8) & 0xFF;
        header[1] = frame_seq & 0xFF;
        header[2] = H264_FLAG_SINGLE;
        header[3] = H264_PKT_TYPE_VIDEO;
        header[4] = (size >> 24) & 0xFF;
        header[5] = (size >> 16) & 0xFF;
        header[6] = (size >> 8) & 0xFF;
        header[7] = size & 0xFF;

        struct iovec iov[2];
        iov[0].iov_base = header;
        iov[0].iov_len = H264_UDP_HEADER_SIZE;
        iov[1].iov_base = (void *)data;
        iov[1].iov_len = size;

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = (void *)dst;
        msg.msg_namelen = sizeof(*dst);
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        int retries = 0;
        while (retries < 50) {
            ssize_t sent = sendmsg(sock, &msg, 0);
            if (sent > 0) return 0;
            if (errno == ENOBUFS || errno == EAGAIN) {
                retries++;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            return -1;
        }
        return -1;
    }

    /* ---- Multi-packet mode (fragmentation) ---- */
    uint8_t flag = H264_FLAG_FIRST;

    while (offset < size) {
        uint32_t remaining = size - offset;
        uint32_t chunk = (remaining > H264_UDP_MAX_PAYLOAD) ? H264_UDP_MAX_PAYLOAD : remaining;

        if (offset > 0 && (offset + chunk) < size) {
            flag = H264_FLAG_MIDDLE;
        } else if ((offset + chunk) >= size) {
            flag = H264_FLAG_LAST;
        }

        header[0] = (frame_seq >> 8) & 0xFF;
        header[1] = frame_seq & 0xFF;
        header[2] = flag;
        header[3] = H264_PKT_TYPE_VIDEO;
        header[4] = (size >> 24) & 0xFF;
        header[5] = (size >> 16) & 0xFF;
        header[6] = (size >> 8) & 0xFF;
        header[7] = size & 0xFF;

        struct iovec iov[2];
        iov[0].iov_base = header;
        iov[0].iov_len = H264_UDP_HEADER_SIZE;
        iov[1].iov_base = (void *)(data + offset);
        iov[1].iov_len = chunk;

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = (void *)dst;
        msg.msg_namelen = sizeof(*dst);
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        int retries = 0;
        while (retries < 50) {
            ssize_t sent = sendmsg(sock, &msg, 0);
            if (sent > 0) break;
            if (errno == ENOBUFS || errno == EAGAIN) {
                retries++;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            return -1;
        }
        offset += chunk;
    }

    return 0;
}


/* ================================================================
 *  Handshake Protocol
 * ================================================================ */
int h264_handshake_send_ready(int sock, const struct sockaddr_in *dst)
{
    uint8_t ready_pkt[H264_UDP_HEADER_SIZE] = {0};
    ready_pkt[3] = H264_PKT_TYPE_READY;
    return sendto(sock, ready_pkt, H264_UDP_HEADER_SIZE, 0,
                  (const struct sockaddr *)dst, sizeof(*dst));
}

bool h264_handshake_is_ack(const uint8_t *data, int len)
{
    return (len >= H264_UDP_HEADER_SIZE && data[3] == H264_PKT_TYPE_ACK);
}

bool h264_handshake_is_hello(const uint8_t *data, int len)
{
    return (len >= H264_UDP_HEADER_SIZE);
}

bool h264_handshake_check_timeout(int64_t start_us, int retry_count)
{
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    return (elapsed_ms > H264_HANDSHAKE_TIMEOUT_MS);
}

bool h264_handshake_should_retry(int64_t start_us, int retry_count)
{
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    return (elapsed_ms > H264_HANDSHAKE_RETRY_MS * (retry_count + 1));
}
