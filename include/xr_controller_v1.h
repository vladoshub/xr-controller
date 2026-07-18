/*
 * Copyright 2026 XR IMU contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef XR_CONTROLLER_V1_H_
#define XR_CONTROLLER_V1_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XR_CONTROLLER_V1_MAGIC_0 ((uint8_t)'X')
#define XR_CONTROLLER_V1_MAGIC_1 ((uint8_t)'C')
#define XR_CONTROLLER_V1_MAGIC_2 ((uint8_t)'T')
#define XR_CONTROLLER_V1_MAGIC_3 ((uint8_t)'L')
#define XR_CONTROLLER_V1_VERSION 1U

#define XR_CONTROLLER_V1_FLAG_TIMESTAMP_VALID 0x01U
#define XR_CONTROLLER_V1_FLAG_CONTROLS_VALID  0x02U
#define XR_CONTROLLER_V1_FLAG_BATTERY_VALID   0x04U

#define XR_CONTROLLER_V1_PACKET_SIZE 64U
#define XR_CONTROLLER_V1_CRC_OFFSET 60U
#define XR_CONTROLLER_V1_AXIS_COUNT 4U

enum xr_controller_v1_button_bits {
    XR_CONTROLLER_V1_BUTTON_A           = UINT32_C(1) << 0,
    XR_CONTROLLER_V1_BUTTON_B           = UINT32_C(1) << 1,
    XR_CONTROLLER_V1_BUTTON_C           = UINT32_C(1) << 2,
    XR_CONTROLLER_V1_BUTTON_TRIGGER     = UINT32_C(1) << 3,
    XR_CONTROLLER_V1_BUTTON_GRIP        = UINT32_C(1) << 4,
    XR_CONTROLLER_V1_BUTTON_MENU        = UINT32_C(1) << 5,
    XR_CONTROLLER_V1_BUTTON_STICK_CLICK = UINT32_C(1) << 6,
    XR_CONTROLLER_V1_BUTTON_DPAD_UP     = UINT32_C(1) << 7,
    XR_CONTROLLER_V1_BUTTON_DPAD_DOWN   = UINT32_C(1) << 8,
    XR_CONTROLLER_V1_BUTTON_DPAD_LEFT   = UINT32_C(1) << 9,
    XR_CONTROLLER_V1_BUTTON_DPAD_RIGHT  = UINT32_C(1) << 10,
};

enum xr_controller_v1_axis_index {
    XR_CONTROLLER_V1_AXIS_THUMBSTICK_X = 0,
    XR_CONTROLLER_V1_AXIS_THUMBSTICK_Y = 1,
    XR_CONTROLLER_V1_AXIS_TRIGGER = 2,
    XR_CONTROLLER_V1_AXIS_GRIP = 3,
};

typedef struct xr_controller_v1_sample {
    uint32_t sequence;
    uint64_t timestamp_us;
    float gyro_rad_s[3];
    float accel_m_s2[3];
    uint32_t buttons;
    int16_t axes[XR_CONTROLLER_V1_AXIS_COUNT];
    uint16_t battery_mv;
    uint16_t controller_status;
    uint8_t flags;
} xr_controller_v1_sample_t;

uint32_t xr_controller_crc32_ieee(const uint8_t *data, size_t size);

void xr_controller_v1_encode(
    uint8_t packet[XR_CONTROLLER_V1_PACKET_SIZE],
    const xr_controller_v1_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* XR_CONTROLLER_V1_H_ */
