#!/usr/bin/env python3
"""Validate a live xr_imu_v1 stream from capture hardware."""
from __future__ import annotations

import argparse
from collections import deque
import statistics
import sys
import time

try:
    import serial  # type: ignore
except ImportError as exc:
    raise SystemExit("pyserial is required: pip install -r requirements-host.txt") from exc

from xr_imu_v1 import StreamParser, TIMESTAMP_VALID


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="e.g. /dev/ttyACM0 or COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=10.0)
    parser.add_argument("--expected-rate", type=float, default=208.0)
    args = parser.parse_args()

    stream = StreamParser()
    host_start = time.monotonic()
    deadline = host_start + args.duration
    count = 0
    bytes_read = 0
    status_buffer = bytearray()
    reported_status: set[str] = set()
    sequence_drops = 0
    last_sequence: int | None = None
    last_timestamp: int | None = None
    timestamp_deltas: deque[int] = deque(maxlen=4096)

    with serial.Serial(args.port, args.baud, timeout=0.1) as port:
        while time.monotonic() < deadline:
            chunk = port.read(4096)
            bytes_read += len(chunk)
            status_buffer.extend(chunk)
            while b"\n" in status_buffer:
                line, _, remainder = status_buffer.partition(b"\n")
                status_buffer = bytearray(remainder)
                marker = b"XRIMU_STATUS:"
                marker_pos = line.find(marker)
                if marker_pos >= 0:
                    text = line[marker_pos:].decode("ascii", errors="replace")
                    if text not in reported_status:
                        print(f"firmware status:   {text}")
                        reported_status.add(text)
            if len(status_buffer) > 256:
                del status_buffer[:-256]

            for sample in stream.feed(chunk):
                count += 1
                if not (sample.flags & TIMESTAMP_VALID):
                    print("error: timestamp-valid flag is not set", file=sys.stderr)
                    return 2

                if last_sequence is not None:
                    gap = (sample.sequence - last_sequence) & 0xFFFFFFFF
                    if gap == 0:
                        print("error: repeated sequence", file=sys.stderr)
                        return 2
                    if gap > 1:
                        sequence_drops += gap - 1

                if last_timestamp is not None:
                    if sample.timestamp_us <= last_timestamp:
                        print("error: device timestamp is not strictly monotonic", file=sys.stderr)
                        return 2
                    timestamp_deltas.append(sample.timestamp_us - last_timestamp)

                last_sequence = sample.sequence
                last_timestamp = sample.timestamp_us

    elapsed = time.monotonic() - host_start
    rate = count / elapsed if elapsed else 0.0
    median_period = statistics.median(timestamp_deltas) if timestamp_deltas else 0.0
    device_rate = 1_000_000.0 / median_period if median_period else 0.0

    print(f"bytes received:     {bytes_read}")
    print(f"valid packets:      {count}")
    print(f"host-observed rate: {rate:.2f} Hz")
    print(f"device median rate: {device_rate:.2f} Hz")
    print(f"sequence drops:     {sequence_drops}")
    print(f"bad CRC/packets:    {stream.bad_packets}")
    print(f"discarded bytes:    {stream.discarded_bytes}")

    minimum_rate = args.expected_rate * 0.90
    if bytes_read == 0:
        print("hint: no UART bytes received; press RESET or power-cycle the board", file=sys.stderr)
    elif count == 0 and not reported_status:
        print("hint: bytes arrived but neither XIMU packets nor firmware status were decoded", file=sys.stderr)

    if count == 0 or device_rate < minimum_rate or stream.bad_packets != 0:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
