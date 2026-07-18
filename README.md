# XR IMU firmware for XIAO nRF54L15 Sense

Open-source upstream-Zephyr firmware that exposes the onboard LSM6DS3TR-C as a standalone `xr_imu_v1` source for XR Gate `capture_service_cpp`.

## Data path

```text
LSM6DS3TR-C (400 kHz I2C, 208 Hz)
        -> Zephyr sensor API
        -> SI units: rad/s and m/s²
        -> explicit xr_imu_v1 encoder + IEEE CRC32
        -> uart20 at 115200 baud
        -> onboard SAMD11 CMSIS-DAP USB CDC bridge
        -> /dev/ttyACM* or COM*
```

The firmware sends no text logs on the serial channel. It performs no orientation fusion and no startup gyro-bias subtraction. Device timestamps are taken at the midpoint of the I2C sample read, not at packet transmission time.

## Fixed sensor configuration

- Output data rate: **208 Hz**
- Gyroscope range: **±2000 dps**; transmitted in rad/s
- Accelerometer range: **±16 g**; transmitted in m/s²
- Packet size: **48 bytes**
- Serial rate: **115200 baud, 8N1**
- UART transport uses one asynchronous EasyDMA transfer per complete packet; it does not send the binary stream byte-by-byte with `uart_poll_out()`.
- Axis order: native Zephyr/LSM6DSL XYZ order

## Supported platform

- Seeed Studio XIAO nRF54L15 Sense
- Board target: `xiao_nrf54l15/nrf54l15/cpuapp`
- Zephyr: `v4.4.0`, pinned in `west.yml`
- Zephyr SDK: `1.0.1`
- Development host: Ubuntu 24.04

## Local setup on Ubuntu 24.04

Clone this repository as a direct child of an empty workspace directory:

```bash
mkdir -p ~/src/xr-imu-workspace
cd ~/src/xr-imu-workspace
git clone <repository-url> xr-imu-xiao-nrf54l15-sense
cd xr-imu-xiao-nrf54l15-sense
./scripts/setup_ubuntu24.sh
```

The setup script installs Ubuntu packages, creates `../.venv-zephyr`, initializes the west workspace, fetches the pinned Zephyr modules, installs Python requirements, and installs the Arm Zephyr SDK toolchain.

Build:

```bash
./scripts/build.sh
```

Artifacts are produced under:

```text
build/xiao_nrf54l15/zephyr/zephyr.elf
build/xiao_nrf54l15/zephyr/zephyr.hex
build/xiao_nrf54l15/zephyr/zephyr.bin
```

Flash through the board's built-in SAMD11 CMSIS-DAP debugger. The same USB-C connection also exposes the CDC serial port used by the firmware:

```bash
./scripts/flash.sh
```

The default runner is OpenOCD. Override it when needed:

```bash
RUNNER=jlink ./scripts/flash.sh
```

## Verify the live stream

Find the CDC port:

```bash
ls -l /dev/serial/by-id/ 2>/dev/null || true
ls -l /dev/ttyACM*
```

Validate packets, CRC, timestamps, sequence continuity, and device sample rate:

```bash
./scripts/verify.sh /dev/ttyACM0
```

Run protocol tests without downloading Zephyr:

```bash
./scripts/test_host.sh
```

## `capture_service_cpp` configuration

A ready-to-copy example is in `config/capture_service_cpp.yaml`:

```yaml
imu:
  enabled: true
  driver: serial
  slot_count: 2048
  output:
    stream: imu0
    frame: imu0
  raw:
    enabled: false
  serial:
    port:
      linux: /dev/ttyACM0
      windows: COM5
    baud_rate: 115200
    protocol: xr_imu_v1
    timestamp_mode: device
    read_timeout_ms: 50
    max_packet_size: 256
  stall_exit_ms: 2000
```

For a stable Linux path, prefer the matching `/dev/serial/by-id/...` symlink after checking the actual identifier reported by the board.

## Transport behavior

Acquisition and serial output use separate threads. The acquisition loop never waits for UART transmission. If the 128-packet TX queue fills, the new sample is dropped and the source sequence advances, so the host can detect the loss instead of receiving stale timing.

At 208 Hz, the binary payload is 9984 bytes/s before UART framing. With
8N1 framing it consumes 99840 bit/s, which fits within the board's known-good
115200-baud SAMD11 USB-UART path. Sampling deadlines are derived from a common
epoch, so the fractional 208 Hz period alternates between 4807 and 4808
microseconds without cumulative integer-division drift.

## Protocol

See [`docs/xr_imu_v1.md`](docs/xr_imu_v1.md). The implementation serializes fields explicitly in little-endian order and is cross-tested against Python's `zlib.crc32`.

## GitHub Actions

- `CI`: host protocol tests, shell syntax validation, Zephyr firmware build, artifact upload.
- `Release`: builds tagged versions and publishes firmware files plus SHA-256 checksums to a GitHub Release.

## License

Apache-2.0. Zephyr and its modules retain their own upstream licenses.

### Kconfig warning about `CMSIS_CORE_HAS_SYSTEM_CORE_CLOCK`

The nRF54L15 target uses CMSIS-Core v6. The west manifest therefore includes
both `cmsis_6` and `hal_nordic` in its module allowlist. If an existing
workspace was created with an older revision of this repository, update the
newly added module before rebuilding:

```bash
cd ..
source .venv-zephyr/bin/activate
west update --narrow -o=--depth=1
cd xr-imu-xiao-nrf54l15-sense
PRISTINE=always ./scripts/build.sh
```

### Serial framing diagnostics

If the verifier reports many discarded bytes but zero `XIMU` packets, run:

```bash
./scripts/probe_serial.sh /dev/ttyACM0
```

The default firmware and host configuration use 115200 baud, matching the
upstream board console configuration and Seeed's documented serial-monitor
setting. The packet rate is 208 Hz so the fixed 48-byte `xr_imu_v1` packets
remain within the available 8N1 line rate.
`verify.sh` also accepts an explicit second argument for diagnostics:

```bash
./scripts/verify.sh /dev/ttyACM0 115200
```

### XIAO UART pin ownership

The board-level devicetree enables `spi22` and `uart20`, but both use P1.8/P1.9.
This application explicitly disables `spi22` so the UART connected to the
on-board SAMD11 CDC bridge retains those pins. `uart21` is also disabled because
P2.7 overlaps the SWO/debug domain and this firmware does not use it.

### Startup diagnostics

The UART remains a binary `xr_imu_v1` stream during normal operation. Short
`XRIMU_STATUS:` lines are emitted only during startup or repeatedly when the
firmware cannot initialize/read the IMU. `scripts/verify.sh` displays these
lines before its packet statistics. The host parser can resynchronize on the
next `XIMU` packet after a startup status line.


### `XRIMU_STATUS:IMU_NOT_READY`

The Sense board powers the LSM6DS3TR-C from the `pdm_imu_pwr` fixed regulator
controlled by GPIO P0.1. The firmware therefore requires both
`CONFIG_REGULATOR=y` and `CONFIG_REGULATOR_FIXED=y`. Without them the UART is
operational, but the sensor driver fails during boot because the IMU rail is off.

Check the generated configuration with:

```bash
grep -E '^CONFIG_(REGULATOR|REGULATOR_FIXED)=' \
  build/xiao_nrf54l15/zephyr/.config
```

Both options must be `y`. Rebuild with `PRISTINE=always` after changing them.
