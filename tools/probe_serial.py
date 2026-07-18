#!/usr/bin/env python3
"""Probe common UART rates and report whether xr_controller_v1 framing is visible."""
from __future__ import annotations

import argparse
import time

try:
    import serial  # type: ignore
except ImportError as exc:
    raise SystemExit("pyserial is required: pip install -r requirements-host.txt") from exc

from xr_controller_v1 import MAGIC, StreamParser

DEFAULT_BAUDS = (115200, 230400, 250000, 460800, 921600, 1000000)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--duration", type=float, default=1.0)
    parser.add_argument("--bauds", type=int, nargs="*", default=DEFAULT_BAUDS)
    args = parser.parse_args()

    if args.duration <= 0:
        parser.error("--duration must be greater than zero")

    best_baud = None
    best_valid = -1

    for baud in args.bauds:
        stream = StreamParser()
        data = bytearray()
        deadline = time.monotonic() + args.duration

        try:
            with serial.Serial(args.port, baud, timeout=0.05) as port:
                port.reset_input_buffer()
                while time.monotonic() < deadline:
                    chunk = port.read(4096)
                    data.extend(chunk)
                    stream.feed(chunk)
        except serial.SerialException as exc:
            print(f"{baud:7d}: serial error: {exc}")
            continue

        # Re-run once to count decoded packets without discarding the return values.
        parser2 = StreamParser()
        samples = parser2.feed(bytes(data))
        magic_count = bytes(data).count(MAGIC)
        preview = bytes(data[:24]).hex(" ") if data else "<no data>"
        print(
            f"{baud:7d}: bytes={len(data):7d} "
            f"magic={magic_count:4d} valid={len(samples):4d} "
            f"bad={parser2.bad_packets:4d} preview={preview}"
        )

        if len(samples) > best_valid:
            best_valid = len(samples)
            best_baud = baud

    if best_baud is not None and best_valid > 0:
        print(f"recommended baud: {best_baud} ({best_valid} valid packets)")
        return 0

    print("no valid xr_controller_v1 packets found at the tested baud rates")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
