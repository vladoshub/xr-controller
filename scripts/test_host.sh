#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host-tests"
mkdir -p "${BUILD_DIR}"

python3 - "${ROOT_DIR}/VERSION" <<'PY_VERSION'
from pathlib import Path
import re
import sys

version_path = Path(sys.argv[1])
text = version_path.read_text(encoding="utf-8")
required = ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL", "VERSION_TWEAK")
for key in required:
    match = re.search(rf"(?m)^{key}\s*=\s*([0-9]+)\s*$", text)
    if match is None or not 0 <= int(match.group(1)) <= 255:
        raise SystemExit(f"Invalid Zephyr application VERSION field: {key}")
if re.search(r"(?m)^EXTRAVERSION\s*=\s*[a-z0-9.-]*\s*$", text) is None:
    raise SystemExit("Invalid Zephyr application VERSION field: EXTRAVERSION")
print("Zephyr application VERSION format: OK")
PY_VERSION

python3 - "${ROOT_DIR}/west.yml" <<'PY_MANIFEST'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")
for required in ("cmsis_6", "hal_nordic"):
    marker = f"          - {required}\n"
    if marker not in text:
        raise SystemExit(f"west.yml module allowlist is missing: {required}")
print("Zephyr module allowlist: OK")
PY_MANIFEST

python3 - "${ROOT_DIR}/prj.conf" <<'PY_CONFIG'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")
for required in (
    "CONFIG_REGULATOR=y",
    "CONFIG_REGULATOR_FIXED=y",
    "CONFIG_UART_ASYNC_API=y",
    "CONFIG_UART_ASYNC_TX_CACHE_SIZE=64",
):
    if required not in text.splitlines():
        raise SystemExit(f"prj.conf is missing required option: {required}")
print("IMU power and asynchronous UART configuration: OK")
PY_CONFIG

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"${ROOT_DIR}/include" \
  "${ROOT_DIR}/src/xr_imu_v1.c" \
  "${ROOT_DIR}/tests/host/test_xr_imu_v1.c" \
  -o "${BUILD_DIR}/test_xr_imu_v1"

"${BUILD_DIR}/test_xr_imu_v1" > "${BUILD_DIR}/c_packet.bin"
python3 -m unittest discover -s "${ROOT_DIR}/tests/python" -p 'test_*.py' -v

PYTHONPATH="${ROOT_DIR}/tools" python3 - "${BUILD_DIR}/c_packet.bin" <<'PY'
from pathlib import Path
import sys
from xr_imu_v1 import decode, encode

packet = Path(sys.argv[1]).read_bytes()
sample = decode(packet)
assert encode(sample) == packet
print("C/Python canonical packet match: OK")
PY
