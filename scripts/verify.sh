#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd -- "${ROOT_DIR}/.." && pwd)"
VENV_DIR="${WORKSPACE_DIR}/.venv-zephyr"
PORT="${1:-/dev/ttyACM0}"
BAUD="${2:-${XR_IMU_BAUD:-115200}}"

if [[ -f "${VENV_DIR}/bin/activate" ]]; then
  # shellcheck disable=SC1091
  source "${VENV_DIR}/bin/activate"
fi

PYTHONPATH="${ROOT_DIR}/tools" \
  python3 "${ROOT_DIR}/tools/verify_serial.py" \
    --port "${PORT}" \
    --baud "${BAUD}"
