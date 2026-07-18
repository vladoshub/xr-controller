# `xr_imu_v1` wire protocol

All multi-byte values are little-endian. Each packet is exactly 48 bytes.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `char[4]` | ASCII magic `XIMU` |
| 4 | `uint8` | Version `1` |
| 5 | `uint8` | Flags; bit 0 means timestamp valid |
| 6 | `uint16` | Packet size `48` |
| 8 | `uint32` | Monotonic sequence |
| 12 | `uint64` | Acquisition timestamp, microseconds |
| 20 | `float32[3]` | Gyroscope XYZ, rad/s |
| 32 | `float32[3]` | Accelerometer XYZ, m/s² |
| 44 | `uint32` | IEEE CRC32 over bytes `[0, 44)` |

The firmware explicitly serializes every field and does not transmit a packed C structure.
