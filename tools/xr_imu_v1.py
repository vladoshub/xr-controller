#!/usr/bin/env python3
"""Host-side xr_imu_v1 codec and stream parser."""
from __future__ import annotations

from dataclasses import dataclass
import math
import struct
import zlib

MAGIC = b"XIMU"
VERSION = 1
TIMESTAMP_VALID = 0x01
PACKET_SIZE = 48
CRC_OFFSET = 44
_STRUCT_WITHOUT_CRC = struct.Struct("<4sBBHIQ3f3f")
_CRC = struct.Struct("<I")


@dataclass(frozen=True)
class Sample:
    sequence: int
    timestamp_us: int
    gyro_rad_s: tuple[float, float, float]
    accel_m_s2: tuple[float, float, float]
    flags: int = TIMESTAMP_VALID


def encode(sample: Sample) -> bytes:
    prefix = _STRUCT_WITHOUT_CRC.pack(
        MAGIC,
        VERSION,
        sample.flags,
        PACKET_SIZE,
        sample.sequence,
        sample.timestamp_us,
        *sample.gyro_rad_s,
        *sample.accel_m_s2,
    )
    assert len(prefix) == CRC_OFFSET
    return prefix + _CRC.pack(zlib.crc32(prefix) & 0xFFFFFFFF)


def decode(packet: bytes) -> Sample:
    if len(packet) != PACKET_SIZE:
        raise ValueError(f"packet size {len(packet)} != {PACKET_SIZE}")

    magic, version, flags, size, sequence, timestamp_us, *values = _STRUCT_WITHOUT_CRC.unpack(
        packet[:CRC_OFFSET]
    )
    expected_crc = _CRC.unpack(packet[CRC_OFFSET:])[0]
    actual_crc = zlib.crc32(packet[:CRC_OFFSET]) & 0xFFFFFFFF

    if magic != MAGIC:
        raise ValueError(f"bad magic: {magic!r}")
    if version != VERSION:
        raise ValueError(f"bad version: {version}")
    if size != PACKET_SIZE:
        raise ValueError(f"bad embedded size: {size}")
    if actual_crc != expected_crc:
        raise ValueError(f"CRC mismatch: got 0x{expected_crc:08x}, expected 0x{actual_crc:08x}")
    if not all(math.isfinite(value) for value in values):
        raise ValueError("packet contains non-finite sensor values")

    return Sample(
        sequence=sequence,
        timestamp_us=timestamp_us,
        gyro_rad_s=(values[0], values[1], values[2]),
        accel_m_s2=(values[3], values[4], values[5]),
        flags=flags,
    )


class StreamParser:
    """Resynchronizing parser for arbitrary serial byte chunks."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.bad_packets = 0
        self.discarded_bytes = 0

    def feed(self, data: bytes) -> list[Sample]:
        self._buffer.extend(data)
        samples: list[Sample] = []

        while True:
            magic_pos = self._buffer.find(MAGIC)
            if magic_pos < 0:
                keep = min(len(self._buffer), len(MAGIC) - 1)
                self.discarded_bytes += len(self._buffer) - keep
                if keep:
                    del self._buffer[:-keep]
                else:
                    self._buffer.clear()
                break

            if magic_pos:
                self.discarded_bytes += magic_pos
                del self._buffer[:magic_pos]

            if len(self._buffer) < PACKET_SIZE:
                break

            candidate = bytes(self._buffer[:PACKET_SIZE])
            try:
                samples.append(decode(candidate))
                del self._buffer[:PACKET_SIZE]
            except ValueError:
                self.bad_packets += 1
                self.discarded_bytes += 1
                del self._buffer[0]

        return samples
