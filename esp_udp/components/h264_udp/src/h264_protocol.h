/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 *
 * H.264 Protocol - Internal header for UDP packet management
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Socket Management ---- */

/** Create and bind UDP socket for H.264 streaming */
int h264_udp_socket_create(void);

/** Close the UDP socket */
void h264_udp_socket_close(int sock);

/* ---- Packet Fragmentation ---- */

/**
 * @brief Send a complete H.264 frame via UDP with automatic fragmentation
 *
 * @param sock  UDP socket
 * @param dst   Destination address
 * @param data  Frame data
 * @param size  Frame size in bytes
 * @param seq   Frame sequence counter (incremented on each call)
 * @return 0 on success, -1 on error
 */
int h264_udp_send_frame(int sock, const struct sockaddr_in *dst,
                        const uint8_t *data, uint32_t size, uint16_t *seq);

/* ---- Handshake ---- */

/** Send READY signal to client */
int h264_handshake_send_ready(int sock, const struct sockaddr_in *dst);

/** Check if received packet is an ACK */
bool h264_handshake_is_ack(const uint8_t *data, int len);

/** Check if received packet is a HELLO from client */
bool h264_handshake_is_hello(const uint8_t *data, int len);

/** Check if handshake has timed out */
bool h264_handshake_check_timeout(int64_t start_us, int retry_count);

/** Check if handshake should retry sending READY */
bool h264_handshake_should_retry(int64_t start_us, int retry_count);

#ifdef __cplusplus
}
#endif
