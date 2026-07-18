/*
 * Copyright 2026 XR IMU contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "xr_controller_v1.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint16_t read_u16_le(const uint8_t *src)
{
    return ((uint16_t)src[0]) | ((uint16_t)src[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

int main(void)
{
    xr_controller_v1_sample_t sample = {
        .sequence = 0x01020304U,
        .timestamp_us = UINT64_C(0x0102030405060708),
        .gyro_rad_s = {1.0F, -2.0F, 0.5F},
        .accel_m_s2 = {9.80665F, 0.0F, -9.80665F},
        .buttons = UINT32_C(0x000005a5),
        .axes = {INT16_MIN, INT16_MAX, 1234, -4321},
        .battery_mv = 4123U,
        .controller_status = 0x55aaU,
        .flags = XR_CONTROLLER_V1_FLAG_TIMESTAMP_VALID,
    };
    uint8_t packet[XR_CONTROLLER_V1_PACKET_SIZE];

    xr_controller_v1_encode(packet, &sample);

    assert(packet[0] == 'X' && packet[1] == 'C' &&
           packet[2] == 'T' && packet[3] == 'L');
    assert(packet[4] == XR_CONTROLLER_V1_VERSION);
    assert(read_u16_le(&packet[6]) == XR_CONTROLLER_V1_PACKET_SIZE);
    assert(read_u32_le(&packet[44]) == sample.buttons);
    assert(read_u16_le(&packet[56]) == sample.battery_mv);
    assert(read_u16_le(&packet[58]) == sample.controller_status);
    assert(read_u32_le(&packet[XR_CONTROLLER_V1_CRC_OFFSET]) ==
           xr_controller_crc32_ieee(packet,
                                     XR_CONTROLLER_V1_CRC_OFFSET));

    fwrite(packet, 1U, sizeof(packet), stdout);
    return 0;
}
