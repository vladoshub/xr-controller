/*
 * Copyright 2026 XR IMU contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef XR_IMU_V1_H_
#define XR_IMU_V1_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XR_IMU_V1_MAGIC_0 ((uint8_t)'X')
#define XR_IMU_V1_MAGIC_1 ((uint8_t)'I')
#define XR_IMU_V1_MAGIC_2 ((uint8_t)'M')
#define XR_IMU_V1_MAGIC_3 ((uint8_t)'U')
#define XR_IMU_V1_VERSION 1U
#define XR_IMU_V1_FLAG_TIMESTAMP_VALID 0x01U
#define XR_IMU_V1_PACKET_SIZE 48U
#define XR_IMU_V1_CRC_OFFSET 44U

typedef struct xr_imu_v1_sample {
    uint32_t sequence;
    uint64_t timestamp_us;
    float gyro_rad_s[3];
    float accel_m_s2[3];
    uint8_t flags;
} xr_imu_v1_sample_t;

uint32_t xr_imu_crc32_ieee(const uint8_t *data, size_t size);

void xr_imu_v1_encode(uint8_t packet[XR_IMU_V1_PACKET_SIZE],
                      const xr_imu_v1_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* XR_IMU_V1_H_ */
