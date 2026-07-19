# `xr_controller_v1` wire protocol

All multi-byte values are little-endian. Each `XCTL` IMU packet remains exactly 64 bytes.
The firmware explicitly serializes every field and never transmits a packed C
structure.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `char[4]` | ASCII magic `XCTL` |
| 4 | `uint8` | Version `1` |
| 5 | `uint8` | Flags: bit 0 timestamp valid, bit 1 controls valid, bit 2 battery valid |
| 6 | `uint16` | Packet size `64` |
| 8 | `uint32` | Monotonic sequence |
| 12 | `uint64` | Acquisition timestamp, microseconds |
| 20 | `float32[3]` | Gyroscope XYZ, rad/s |
| 32 | `float32[3]` | Accelerometer XYZ, m/s² |
| 44 | `uint32` | Digital button-state bitmap |
| 48 | `int16[4]` | Thumbstick X/Y, trigger and grip/aux axes |
| 56 | `uint16` | Battery voltage, millivolts; zero when unavailable |
| 58 | `uint16` | Controller status bitmap; currently reserved |
| 60 | `uint32` | IEEE CRC32 over bytes `[0, 60)` |

## Button bitmap

| Bit | Meaning |
|---:|---|
| 0 | A |
| 1 | B |
| 2 | C |
| 3 | Trigger |
| 4 | Grip |
| 5 | Menu |
| 6 | Thumbstick click |
| 7 | D-pad up |
| 8 | D-pad down |
| 9 | D-pad left |
| 10 | D-pad right |
| 11–31 | Reserved; transmit as zero |

The current firmware only populates IMU, sequence and timestamp fields. Control,
battery and status fields are reserved in the packet now so future firmware can
add GPIO buttons and analog inputs without changing framing or requiring another
`capture_service_cpp` protocol migration.


## `XCID` v1 identity frame

The firmware additionally emits a separate 32-byte identity frame at startup
and approximately once per second. It is queued independently and does not
replace or reduce the 208 Hz `XCTL` samples.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `char[4]` | ASCII magic `XCID` |
| 4 | `uint8` | Identity version `1` |
| 5 | `uint8` | Flags; bit 0 means device UID valid |
| 6 | `uint16` | Packet size `32` |
| 8 | `uint8` | Device UID byte count, maximum `16` |
| 9 | `uint8` | Associated `XCTL` version (`1`) |
| 10 | `uint16` | Reserved |
| 12 | `uint8[16]` | Opaque bytes returned by Zephyr `hwinfo_get_device_id()` |
| 28 | `uint32` | IEEE CRC32 over bytes `[0, 28)` |

Hosts represent the UID as lowercase hexadecimal, for example
`0123456789abcdef`. If Zephyr cannot provide a hardware UID, the valid flag is
clear and host software falls back to the configured serial port identity.
