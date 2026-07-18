/*
 * Copyright 2026 XR IMU contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "xr_imu_v1.h"

#include <string.h>

_Static_assert(sizeof(float) == 4U, "xr_imu_v1 requires IEEE-754 binary32 float");

static void put_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void put_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8U) & 0xffU);
    dst[2] = (uint8_t)((value >> 16U) & 0xffU);
    dst[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static void put_u64_le(uint8_t *dst, uint64_t value)
{
    for (size_t i = 0U; i < 8U; ++i) {
        dst[i] = (uint8_t)((value >> (8U * i)) & 0xffU);
    }
}

static void put_f32_le(uint8_t *dst, float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    put_u32_le(dst, bits);
}

uint32_t xr_imu_crc32_ieee(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffU;

    for (size_t i = 0U; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }

    return crc ^ 0xffffffffU;
}

void xr_imu_v1_encode(uint8_t packet[XR_IMU_V1_PACKET_SIZE],
                      const xr_imu_v1_sample_t *sample)
{
    memset(packet, 0, XR_IMU_V1_PACKET_SIZE);

    packet[0] = XR_IMU_V1_MAGIC_0;
    packet[1] = XR_IMU_V1_MAGIC_1;
    packet[2] = XR_IMU_V1_MAGIC_2;
    packet[3] = XR_IMU_V1_MAGIC_3;
    packet[4] = XR_IMU_V1_VERSION;
    packet[5] = sample->flags;
    put_u16_le(&packet[6], XR_IMU_V1_PACKET_SIZE);
    put_u32_le(&packet[8], sample->sequence);
    put_u64_le(&packet[12], sample->timestamp_us);

    for (size_t i = 0U; i < 3U; ++i) {
        put_f32_le(&packet[20U + (i * 4U)], sample->gyro_rad_s[i]);
        put_f32_le(&packet[32U + (i * 4U)], sample->accel_m_s2[i]);
    }

    put_u32_le(&packet[XR_IMU_V1_CRC_OFFSET],
               xr_imu_crc32_ieee(packet, XR_IMU_V1_CRC_OFFSET));
}
