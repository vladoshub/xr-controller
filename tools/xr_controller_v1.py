#!/usr/bin/env python3
"""Host-side xr_controller_v1 codec and mixed XCTL/XCID stream parser."""
from __future__ import annotations

from dataclasses import dataclass
import math
import struct
import zlib

MAGIC = b"XCTL"
VERSION = 1
TIMESTAMP_VALID = 0x01
CONTROLS_VALID = 0x02
BATTERY_VALID = 0x04
PACKET_SIZE = 64
CRC_OFFSET = 60
_STRUCT_WITHOUT_CRC = struct.Struct("<4sBBHIQ3f3fI4hHH")
_CRC = struct.Struct("<I")

IDENTITY_MAGIC = b"XCID"
IDENTITY_VERSION = 1
IDENTITY_DEVICE_UID_VALID = 0x01
IDENTITY_PACKET_SIZE = 32
IDENTITY_CRC_OFFSET = 28
DEVICE_UID_MAX_SIZE = 16
_IDENTITY_PREFIX = struct.Struct("<4sBBHBBH16s")


@dataclass(frozen=True)
class Identity:
    device_uid: bytes
    flags: int = IDENTITY_DEVICE_UID_VALID
    controller_protocol_version: int = VERSION

    @property
    def device_uid_hex(self) -> str:
        if not (self.flags & IDENTITY_DEVICE_UID_VALID):
            return ""
        return self.device_uid.hex()


@dataclass(frozen=True)
class Sample:
    sequence: int
    timestamp_us: int
    gyro_rad_s: tuple[float, float, float]
    accel_m_s2: tuple[float, float, float]
    flags: int = TIMESTAMP_VALID
    buttons: int = 0
    axes: tuple[int, int, int, int] = (0, 0, 0, 0)
    battery_mv: int = 0
    controller_status: int = 0


def encode_identity(identity: Identity) -> bytes:
    uid = bytes(identity.device_uid)
    if len(uid) > DEVICE_UID_MAX_SIZE:
        raise ValueError(f"device UID is too long: {len(uid)} > {DEVICE_UID_MAX_SIZE}")
    prefix = _IDENTITY_PREFIX.pack(
        IDENTITY_MAGIC,
        IDENTITY_VERSION,
        identity.flags,
        IDENTITY_PACKET_SIZE,
        len(uid),
        identity.controller_protocol_version,
        0,
        uid.ljust(DEVICE_UID_MAX_SIZE, b"\0"),
    )
    assert len(prefix) == IDENTITY_CRC_OFFSET
    return prefix + _CRC.pack(zlib.crc32(prefix) & 0xFFFFFFFF)


def decode_identity(packet: bytes) -> Identity:
    if len(packet) != IDENTITY_PACKET_SIZE:
        raise ValueError(
            f"identity packet size {len(packet)} != {IDENTITY_PACKET_SIZE}"
        )
    fields = _IDENTITY_PREFIX.unpack(packet[:IDENTITY_CRC_OFFSET])
    magic, version, flags, size, uid_size, protocol_version, _reserved, uid = fields
    expected_crc = _CRC.unpack(packet[IDENTITY_CRC_OFFSET:])[0]
    actual_crc = zlib.crc32(packet[:IDENTITY_CRC_OFFSET]) & 0xFFFFFFFF
    if magic != IDENTITY_MAGIC:
        raise ValueError(f"bad identity magic: {magic!r}")
    if version != IDENTITY_VERSION:
        raise ValueError(f"bad identity version: {version}")
    if size != IDENTITY_PACKET_SIZE:
        raise ValueError(f"bad identity embedded size: {size}")
    if uid_size > DEVICE_UID_MAX_SIZE:
        raise ValueError(f"bad device UID size: {uid_size}")
    if actual_crc != expected_crc:
        raise ValueError(
            f"identity CRC mismatch: got 0x{expected_crc:08x}, "
            f"expected 0x{actual_crc:08x}"
        )
    return Identity(
        device_uid=uid[:uid_size],
        flags=flags,
        controller_protocol_version=protocol_version,
    )


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
        sample.buttons,
        *sample.axes,
        sample.battery_mv,
        sample.controller_status,
    )
    assert len(prefix) == CRC_OFFSET
    return prefix + _CRC.pack(zlib.crc32(prefix) & 0xFFFFFFFF)


def decode(packet: bytes) -> Sample:
    if len(packet) != PACKET_SIZE:
        raise ValueError(f"packet size {len(packet)} != {PACKET_SIZE}")

    fields = _STRUCT_WITHOUT_CRC.unpack(packet[:CRC_OFFSET])
    magic, version, flags, size, sequence, timestamp_us = fields[:6]
    gyro = fields[6:9]
    accel = fields[9:12]
    buttons = fields[12]
    axes = fields[13:17]
    battery_mv = fields[17]
    controller_status = fields[18]
    expected_crc = _CRC.unpack(packet[CRC_OFFSET:])[0]
    actual_crc = zlib.crc32(packet[:CRC_OFFSET]) & 0xFFFFFFFF

    if magic != MAGIC:
        raise ValueError(f"bad magic: {magic!r}")
    if version != VERSION:
        raise ValueError(f"bad version: {version}")
    if size != PACKET_SIZE:
        raise ValueError(f"bad embedded size: {size}")
    if actual_crc != expected_crc:
        raise ValueError(
            f"CRC mismatch: got 0x{expected_crc:08x}, expected 0x{actual_crc:08x}"
        )
    if not all(math.isfinite(value) for value in (*gyro, *accel)):
        raise ValueError("packet contains non-finite sensor values")

    return Sample(
        sequence=sequence,
        timestamp_us=timestamp_us,
        gyro_rad_s=(gyro[0], gyro[1], gyro[2]),
        accel_m_s2=(accel[0], accel[1], accel[2]),
        flags=flags,
        buttons=buttons,
        axes=(axes[0], axes[1], axes[2], axes[3]),
        battery_mv=battery_mv,
        controller_status=controller_status,
    )


class StreamParser:
    """Resynchronizing parser for mixed periodic XCID and 208 Hz XCTL frames."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.bad_packets = 0
        self.discarded_bytes = 0
        self.identities: list[Identity] = []

    def feed(self, data: bytes) -> list[Sample]:
        self._buffer.extend(data)
        samples: list[Sample] = []

        while True:
            sample_pos = self._buffer.find(MAGIC)
            identity_pos = self._buffer.find(IDENTITY_MAGIC)
            positions = [pos for pos in (sample_pos, identity_pos) if pos >= 0]
            if not positions:
                keep = min(len(self._buffer), len(MAGIC) - 1)
                self.discarded_bytes += len(self._buffer) - keep
                if keep:
                    del self._buffer[:-keep]
                else:
                    self._buffer.clear()
                break

            magic_pos = min(positions)
            if magic_pos:
                self.discarded_bytes += magic_pos
                del self._buffer[:magic_pos]

            if self._buffer.startswith(IDENTITY_MAGIC):
                if len(self._buffer) < IDENTITY_PACKET_SIZE:
                    break
                candidate = bytes(self._buffer[:IDENTITY_PACKET_SIZE])
                try:
                    self.identities.append(decode_identity(candidate))
                    del self._buffer[:IDENTITY_PACKET_SIZE]
                except ValueError:
                    self.bad_packets += 1
                    self.discarded_bytes += 1
                    del self._buffer[0]
                continue

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
