#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from xr_controller_v1 import (  # noqa: E402
    CRC_OFFSET,
    PACKET_SIZE,
    Sample,
    StreamParser,
    decode,
    encode,
)


class ProtocolTest(unittest.TestCase):
    def setUp(self) -> None:
        self.sample = Sample(
            sequence=0x01020304,
            timestamp_us=0x0102030405060708,
            gyro_rad_s=(1.0, -2.0, 0.5),
            accel_m_s2=(9.80665, 0.0, -9.80665),
            buttons=0x000005A5,
            axes=(-32768, 32767, 1234, -4321),
            battery_mv=4123,
            controller_status=0x55AA,
        )

    def test_round_trip(self) -> None:
        packet = encode(self.sample)
        self.assertEqual(len(packet), PACKET_SIZE)
        decoded = decode(packet)
        self.assertEqual(decoded.sequence, self.sample.sequence)
        self.assertEqual(decoded.timestamp_us, self.sample.timestamp_us)
        self.assertEqual(decoded.buttons, self.sample.buttons)
        self.assertEqual(decoded.axes, self.sample.axes)
        self.assertEqual(decoded.battery_mv, self.sample.battery_mv)
        self.assertEqual(decoded.controller_status, self.sample.controller_status)
        for actual, expected in zip(decoded.gyro_rad_s, self.sample.gyro_rad_s):
            self.assertAlmostEqual(actual, expected, places=5)
        for actual, expected in zip(decoded.accel_m_s2, self.sample.accel_m_s2):
            self.assertAlmostEqual(actual, expected, places=5)

    def test_crc_rejection(self) -> None:
        packet = bytearray(encode(self.sample))
        packet[CRC_OFFSET - 1] ^= 0x80
        with self.assertRaisesRegex(ValueError, "CRC mismatch"):
            decode(bytes(packet))

    def test_stream_resynchronization(self) -> None:
        parser = StreamParser()
        first = encode(self.sample)
        second = encode(Sample(2, 20, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)))
        parsed = []
        parsed += parser.feed(b"noise" + first[:17])
        parsed += parser.feed(first[17:] + b"bad" + second)
        self.assertEqual([s.sequence for s in parsed], [self.sample.sequence, 2])
        self.assertGreaterEqual(parser.discarded_bytes, 8)


if __name__ == "__main__":
    unittest.main()
