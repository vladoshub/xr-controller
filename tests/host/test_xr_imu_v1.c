/*
 * Copyright 2026 XR IMU contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "xr_imu_v1.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t read_u32_le(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

int main(void)
{
    static const uint8_t expected[XR_IMU_V1_PACKET_SIZE] = {
        /* Filled from the canonical Python/zlib test vector. */
        0x58, 0x49, 0x4d, 0x55, 0x01, 0x01, 0x30, 0x00,
        0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05,
        0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x80, 0x3f,
        0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x3f,
        0x0a, 0xe8, 0x1c, 0x41, 0x00, 0x00, 0x00, 0x00,
        0x0a, 0xe8, 0x1c, 0xc1, 0x53, 0x4b, 0x4d, 0x80,
    };
    xr_imu_v1_sample_t sample = {
        .sequence = 0x01020304U,
        .timestamp_us = UINT64_C(0x0102030405060708),
        .gyro_rad_s = {1.0F, -2.0F, 0.5F},
        .accel_m_s2 = {9.80665F, 0.0F, -9.80665F},
        .flags = XR_IMU_V1_FLAG_TIMESTAMP_VALID,
    };
    uint8_t packet[XR_IMU_V1_PACKET_SIZE];

    xr_imu_v1_encode(packet, &sample);

    assert(memcmp(packet, expected, XR_IMU_V1_CRC_OFFSET) == 0);
    assert(read_u32_le(&packet[XR_IMU_V1_CRC_OFFSET]) ==
           xr_imu_crc32_ieee(packet, XR_IMU_V1_CRC_OFFSET));

    fwrite(packet, 1U, sizeof(packet), stdout);
    return 0;
}
