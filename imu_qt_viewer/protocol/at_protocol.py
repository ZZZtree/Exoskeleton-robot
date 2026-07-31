# -*- coding: utf-8 -*-
"""
AT 协议解析模块 — 负责从字节流中提取 IMU 角度数据包。
"""

from typing import Optional, Tuple

from ..config import AT_HEADER, AT_FOOTER, DATA_LEN_TO_ADDR, ANGLE_SCALE


def find_at_packet(buffer: bytearray) -> Tuple[Optional[bytes], bytearray]:
    """在字节缓冲区中查找一个完整的 AT 协议包。

    Args:
        buffer: 待搜索的字节缓冲区（原地修改）。

    Returns:
        (packet, remaining): 完整包字节串和剩余缓冲区。
        packet 为 None 表示未找到完整包。
    """
    # 搜索 header
    header_pos = -1
    for i in range(len(buffer) - len(AT_HEADER) + 1):
        if buffer[i:i + len(AT_HEADER)] == AT_HEADER:
            header_pos = i
            break

    if header_pos == -1:
        buffer.clear()
        return None, buffer

    if header_pos > 0:
        buffer = buffer[header_pos:]

    # 搜索 footer
    footer_pos = -1
    for i in range(len(AT_HEADER), len(buffer) - len(AT_FOOTER) + 1):
        if buffer[i:i + len(AT_FOOTER)] == AT_FOOTER:
            footer_pos = i
            break

    if footer_pos == -1:
        return None, buffer

    packet_end = footer_pos + len(AT_FOOTER)
    return bytes(buffer[:packet_end]), bytearray(buffer[packet_end:])


def parse_angle_data(packet: bytes) -> Optional[dict]:
    """解析 AT 协议角度数据包，提取 X/Y/Z 三轴角度。

    Args:
        packet: 完整的 AT 协议包字节串。

    Returns:
        包含 imu_addr, x_angle, y_angle, z_angle 的字典，或 None。
    """
    if len(packet) < 17:
        return None
    if packet[:2] != AT_HEADER or packet[-2:] != AT_FOOTER:
        return None

    data_len = packet[2]
    if data_len not in DATA_LEN_TO_ADDR:
        return None

    imu_addr = DATA_LEN_TO_ADDR[data_len]
    x_raw = packet[9] | (packet[10] << 8)
    y_raw = packet[11] | (packet[12] << 8)
    z_raw = packet[13] | (packet[14] << 8)

    def raw_to_angle(raw: int) -> float:
        if raw > 32767:
            raw -= 65536
        return raw * ANGLE_SCALE

    return {
        "data_len": data_len,
        "imu_addr": imu_addr,
        "x_angle": raw_to_angle(x_raw),
        "y_angle": raw_to_angle(y_raw),
        "z_angle": raw_to_angle(z_raw),
    }
