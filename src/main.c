/*
 * Copyright 2026 XR IMU contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include "xr_imu_v1.h"

#define XR_IMU_SAMPLE_RATE_HZ 208U
#define XR_IMU_SAMPLE_PERIOD_US_CEIL \
    ((1000000U + XR_IMU_SAMPLE_RATE_HZ - 1U) / XR_IMU_SAMPLE_RATE_HZ)
#define XR_IMU_TX_QUEUE_DEPTH 128U
#define XR_IMU_TX_STACK_SIZE 1536U
#define XR_IMU_TX_PRIORITY 2

struct tx_packet {
    uint8_t bytes[XR_IMU_V1_PACKET_SIZE];
};

K_MSGQ_DEFINE(tx_queue, sizeof(struct tx_packet), XR_IMU_TX_QUEUE_DEPTH, 4);

#define XR_IMU_NODE DT_ALIAS(imu0)
static const struct device *const imu_dev = DEVICE_DT_GET(XR_IMU_NODE);
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(XR_IMU_NODE);
static const struct gpio_dt_spec imu_power =
    GPIO_DT_SPEC_GET(DT_NODELABEL(pdm_imu_pwr), enable_gpios);
#define XR_IMU_UART_NODE DT_NODELABEL(uart20)
#define XR_IMU_UART_BAUD DT_PROP(XR_IMU_UART_NODE, current_speed)
#define XR_IMU_UART_BITS_PER_BYTE 10U
#define XR_IMU_UART_MAX_LOAD_PERCENT 90U

BUILD_ASSERT(
    XR_IMU_SAMPLE_RATE_HZ * XR_IMU_V1_PACKET_SIZE * XR_IMU_UART_BITS_PER_BYTE * 100U <=
        XR_IMU_UART_BAUD * XR_IMU_UART_MAX_LOAD_PERCENT,
    "xr_imu_v1 stream exceeds the configured UART throughput budget");

static const struct device *const uart_dev = DEVICE_DT_GET(XR_IMU_UART_NODE);

#if DT_HAS_ALIAS(led0)
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif

static uint64_t monotonic_time_us(void)
{
    return k_cyc_to_us_floor64(k_cycle_get_64());
}

K_MUTEX_DEFINE(uart_tx_mutex);
K_SEM_DEFINE(uart_tx_done, 0, 1);

static int uart_tx_result;

static void uart_event_callback(const struct device *dev,
                                struct uart_event *event,
                                void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    switch (event->type) {
    case UART_TX_DONE:
        uart_tx_result = 0;
        k_sem_give(&uart_tx_done);
        break;
    case UART_TX_ABORTED:
        uart_tx_result = -EIO;
        k_sem_give(&uart_tx_done);
        break;
    default:
        break;
    }
}

static int uart_write_raw(const uint8_t *data, size_t size)
{
    int rc;

    if (!device_is_ready(uart_dev)) {
        return -ENODEV;
    }
    if (size == 0U) {
        return 0;
    }

    k_mutex_lock(&uart_tx_mutex, K_FOREVER);
    k_sem_reset(&uart_tx_done);
    uart_tx_result = -EINPROGRESS;

    rc = uart_tx(uart_dev, data, size, SYS_FOREVER_US);
    if (rc == 0) {
        rc = k_sem_take(&uart_tx_done, K_MSEC(1000));
        if (rc == 0) {
            rc = uart_tx_result;
        } else {
            (void)uart_tx_abort(uart_dev);
            rc = -ETIMEDOUT;
        }
    }

    k_mutex_unlock(&uart_tx_mutex);
    return rc;
}

static void uart_write_text(const char *text)
{
    size_t size = 0U;

    while (text[size] != '\0') {
        ++size;
    }
    (void)uart_write_raw((const uint8_t *)text, size);
}

static char hex_digit(uint8_t value)
{
    value &= 0x0fU;
    return value < 10U ? (char)('0' + value) : (char)('A' + value - 10U);
}

static void uart_write_status_hex8(const char *prefix, uint8_t value)
{
    char suffix[4] = {hex_digit(value >> 4), hex_digit(value), '\n', '\0'};
    uart_write_text(prefix);
    uart_write_text(suffix);
}

static void uart_write_status_i32(const char *prefix, int32_t value)
{
    char digits[12];
    char out[14];
    size_t count = 0U;
    size_t pos = 0U;
    uint32_t magnitude;

    if (value < 0) {
        out[pos++] = '-';
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }

    do {
        digits[count++] = (char)('0' + (magnitude % 10U));
        magnitude /= 10U;
    } while (magnitude != 0U);

    while (count != 0U) {
        out[pos++] = digits[--count];
    }
    out[pos++] = '\n';
    out[pos] = '\0';

    uart_write_text(prefix);
    uart_write_text(out);
}

static int probe_who_am_i(uint16_t address, uint8_t *value)
{
    return i2c_reg_read_byte(imu_i2c.bus, address, 0x0fU, value);
}

static void fatal_signal(const char *status)
{
#if DT_HAS_ALIAS(led0)
    const bool led_ready = gpio_is_ready_dt(&status_led) &&
        gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE) == 0;
#else
    const bool led_ready = false;
#endif

    while (true) {
        uart_write_text(status);
#if DT_HAS_ALIAS(led0)
        if (led_ready) {
            for (int i = 0; i < 6; ++i) {
                (void)gpio_pin_toggle_dt(&status_led);
                k_sleep(K_MSEC(100));
            }
        }
#endif
        k_sleep(K_MSEC(400));
    }
}

static void tx_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    struct tx_packet packet;

    while (true) {
        (void)k_msgq_get(&tx_queue, &packet, K_FOREVER);
        if (uart_write_raw(packet.bytes, sizeof(packet.bytes)) < 0) {
            k_sleep(K_MSEC(10));
        }
    }
}

K_THREAD_DEFINE(tx_thread_id, XR_IMU_TX_STACK_SIZE, tx_thread,
                NULL, NULL, NULL, XR_IMU_TX_PRIORITY, 0, 0);

int main(void)
{
    struct sensor_value gyro[3];
    struct sensor_value accel[3];
    struct tx_packet packet;
    uint32_t sequence = 0U;
    uint64_t schedule_origin_us;
    uint64_t sample_index = 0U;
    uint32_t consecutive_fetch_errors = 0U;
    bool streaming_announced = false;

    if (!device_is_ready(uart_dev)) {
        fatal_signal("XRIMU_STATUS:UART_NOT_READY\n");
    }

    const int callback_rc = uart_callback_set(uart_dev, uart_event_callback, NULL);
    if (callback_rc < 0) {
        fatal_signal("XRIMU_STATUS:UART_ASYNC_INIT_FAILED\n");
    }

    uart_write_text("XRIMU_STATUS:BOOT\n");

    if (!device_is_ready(imu_i2c.bus)) {
        fatal_signal("XRIMU_STATUS:I2C30_NOT_READY\n");
    }

    if (!gpio_is_ready_dt(&imu_power)) {
        fatal_signal("XRIMU_STATUS:IMU_POWER_GPIO_NOT_READY\n");
    }

    const int power_rc = gpio_pin_configure_dt(&imu_power, GPIO_OUTPUT_ACTIVE);
    if (power_rc < 0) {
        uart_write_status_i32("XRIMU_STATUS:IMU_POWER_RC=", power_rc);
        fatal_signal("XRIMU_STATUS:IMU_POWER_FAILED\n");
    }

    /* The datasheet turn-on delay is short, but leave generous margin before
     * probing and initializing the sensor driver.
     */
    k_sleep(K_MSEC(50));

    uint8_t who_6a = 0U;
    uint8_t who_6b = 0U;
    const int probe_6a_rc = probe_who_am_i(0x6aU, &who_6a);
    const int probe_6b_rc = probe_who_am_i(0x6bU, &who_6b);

    if (probe_6a_rc == 0) {
        uart_write_status_hex8("XRIMU_STATUS:WHOAMI_6A=0x", who_6a);
    } else {
        uart_write_status_i32("XRIMU_STATUS:WHOAMI_6A_RC=", probe_6a_rc);
    }

    if (probe_6b_rc == 0) {
        uart_write_status_hex8("XRIMU_STATUS:WHOAMI_6B=0x", who_6b);
    } else {
        uart_write_status_i32("XRIMU_STATUS:WHOAMI_6B_RC=", probe_6b_rc);
    }

    if (probe_6a_rc != 0 || who_6a != 0x6aU) {
        if (probe_6b_rc == 0 && who_6b == 0x6aU) {
            fatal_signal("XRIMU_STATUS:IMU_FOUND_AT_0X6B\n");
        }
        fatal_signal("XRIMU_STATUS:IMU_NOT_FOUND\n");
    }

    const int init_rc = device_init(imu_dev);
    if (init_rc < 0 && init_rc != -EALREADY) {
        uart_write_status_i32("XRIMU_STATUS:IMU_INIT_RC=", init_rc);
        fatal_signal("XRIMU_STATUS:IMU_INIT_FAILED\n");
    }

    if (!device_is_ready(imu_dev)) {
        uart_write_status_i32("XRIMU_STATUS:IMU_INIT_RC=", init_rc);
        fatal_signal("XRIMU_STATUS:IMU_NOT_READY_AFTER_INIT\n");
    }

    uart_write_text("XRIMU_STATUS:IMU_READY\n");

    /* The fixed Kconfig ODR/full-scale settings are applied by the driver. */
    k_sleep(K_MSEC(20));
    schedule_origin_us = monotonic_time_us();

    while (true) {
        xr_imu_v1_sample_t sample = {
            .flags = XR_IMU_V1_FLAG_TIMESTAMP_VALID,
        };
        const uint64_t fetch_start_us = monotonic_time_us();
        const int fetch_rc = sensor_sample_fetch(imu_dev);
        const uint64_t fetch_end_us = monotonic_time_us();

        const int gyro_rc = fetch_rc == 0 ?
            sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro) : fetch_rc;
        const int accel_rc = (fetch_rc == 0 && gyro_rc == 0) ?
            sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel) : gyro_rc;

        if (fetch_rc == 0 && gyro_rc == 0 && accel_rc == 0) {
            consecutive_fetch_errors = 0U;
            if (!streaming_announced) {
                uart_write_text("XRIMU_STATUS:STREAMING\n");
                streaming_announced = true;
            }

            sample.sequence = sequence++;
            /* Midpoint of the I2C register read is a better acquisition estimate
             * than the later packet-transmit time.
             */
            sample.timestamp_us = fetch_start_us + ((fetch_end_us - fetch_start_us) / 2U);

            for (size_t axis = 0U; axis < 3U; ++axis) {
                sample.gyro_rad_s[axis] = sensor_value_to_float(&gyro[axis]);
                sample.accel_m_s2[axis] = sensor_value_to_float(&accel[axis]);
            }

            xr_imu_v1_encode(packet.bytes, &sample);

            /* Never block acquisition on serial output. A full queue drops this
             * sample; the monotonic sequence exposes the loss to the host.
             */
            (void)k_msgq_put(&tx_queue, &packet, K_NO_WAIT);
        } else {
            ++consecutive_fetch_errors;
            if (consecutive_fetch_errors == XR_IMU_SAMPLE_RATE_HZ) {
                uart_write_text("XRIMU_STATUS:IMU_READ_FAILED\n");
                consecutive_fetch_errors = 0U;
            }
        }

        /* Derive every deadline from the common origin. This distributes the
         * fractional 208 Hz period between 4807 and 4808 us without cumulative
         * integer-division drift.
         */
        ++sample_index;
        uint64_t next_sample_us = schedule_origin_us +
            ((sample_index * 1000000ULL) / XR_IMU_SAMPLE_RATE_HZ);
        const uint64_t now_us = monotonic_time_us();

        if (next_sample_us > now_us) {
            k_sleep(K_USEC(next_sample_us - now_us));
        } else if ((now_us - next_sample_us) >
                   (4U * XR_IMU_SAMPLE_PERIOD_US_CEIL)) {
            /* Do not repeatedly run late after a debugger stop or long stall. */
            schedule_origin_us = now_us;
            sample_index = 0U;
        }
    }

    return 0;
}
