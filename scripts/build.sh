#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd -- "${ROOT_DIR}/.." && pwd)"
VENV_DIR="${WORKSPACE_DIR}/.venv-zephyr"
BOARD="${BOARD:-xiao_nrf54l15/nrf54l15/cpuapp}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/xiao_nrf54l15}"
PRISTINE="${PRISTINE:-auto}"

if [[ -f "${VENV_DIR}/bin/activate" ]]; then
  # shellcheck disable=SC1091
  source "${VENV_DIR}/bin/activate"
fi

if ! command -v west >/dev/null 2>&1; then
  echo "west is not available; run scripts/setup_ubuntu24.sh first." >&2
  exit 1
fi

cd "${WORKSPACE_DIR}"

check_pinned_module() {
  local project="$1"
  local expected actual module_dir

  if ! west list "${project}" >/dev/null 2>&1; then
    echo "Required west project is missing from the manifest: ${project}" >&2
    echo "Apply the current repository manifest and rerun scripts/setup_ubuntu24.sh." >&2
    exit 1
  fi

  module_dir="$(west list "${project}" -f '{abspath}')"
  expected="$(west list "${project}" -f '{revision}')"

  if [[ ! -d "${module_dir}/.git" ]]; then
    echo "Required west module is not downloaded: ${project}" >&2
    echo "Run:" >&2
    echo "  cd ${WORKSPACE_DIR}" >&2
    echo "  west update --narrow -o=--depth=1" >&2
    exit 1
  fi

  actual="$(git -C "${module_dir}" rev-parse HEAD)"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "West module ${project} is at the wrong revision." >&2
    echo "  expected: ${expected}" >&2
    echo "  actual:   ${actual}" >&2
    echo "Run:" >&2
    echo "  cd ${WORKSPACE_DIR}" >&2
    echo "  west update --narrow -o=--depth=1" >&2
    exit 1
  fi
}

check_pinned_module cmsis_6
check_pinned_module hal_nordic

west build \
  --board "${BOARD}" \
  --build-dir "${BUILD_DIR}" \
  --pristine "${PRISTINE}" \
  "${ROOT_DIR}"

printf '\nFirmware artifacts:\n'
for file in zephyr.elf zephyr.hex zephyr.bin zephyr.map; do
  if [[ -f "${BUILD_DIR}/zephyr/${file}" ]]; then
    printf '  %s\n' "${BUILD_DIR}/zephyr/${file}"
  fi
done
