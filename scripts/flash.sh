#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd -- "${ROOT_DIR}/.." && pwd)"
VENV_DIR="${WORKSPACE_DIR}/.venv-zephyr"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/xiao_nrf54l15}"
RUNNER="${RUNNER:-openocd}"

if [[ -f "${VENV_DIR}/bin/activate" ]]; then
  # shellcheck disable=SC1091
  source "${VENV_DIR}/bin/activate"
fi

cd "${WORKSPACE_DIR}"
west flash --build-dir "${BUILD_DIR}" --runner "${RUNNER}" "$@"
