#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd -- "${ROOT_DIR}/.." && pwd)"
APP_DIR_NAME="$(basename -- "${ROOT_DIR}")"
VENV_DIR="${WORKSPACE_DIR}/.venv-zephyr"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This installer targets Ubuntu 24.04." >&2
  exit 1
fi

if [[ -r /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  if [[ "${ID:-}" != "ubuntu" || "${VERSION_ID:-}" != "24.04" ]]; then
    echo "Warning: tested on Ubuntu 24.04; detected ${PRETTY_NAME:-unknown}." >&2
  fi
fi

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential ccache cmake ninja-build gperf git \
  device-tree-compiler dfu-util file wget xz-utils \
  python3 python3-dev python3-pip python3-venv \
  libmagic1 libusb-1.0-0 openocd

sudo usermod -aG dialout,plugdev "${USER}"
echo "Ensured ${USER} is in dialout and plugdev; log out and back in if group membership changed." >&2

python3 -m venv "${VENV_DIR}"
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
python -m pip install --upgrade pip wheel
python -m pip install "west>=1.3,<2"

if [[ ! -d "${WORKSPACE_DIR}/.west" ]]; then
  (
    cd "${WORKSPACE_DIR}"
    west init -l "${APP_DIR_NAME}"
  )
fi

if [[ "$(west topdir)" != "${WORKSPACE_DIR}" ]]; then
  echo "The repository must be a direct child of its west workspace." >&2
  echo "Expected west topdir: ${WORKSPACE_DIR}" >&2
  echo "Actual west topdir:   $(west topdir)" >&2
  exit 1
fi

(
  cd "${WORKSPACE_DIR}"
  west update --narrow -o=--depth=1

  # nRF54L15 uses CMSIS-Core v6. Verify that the slim manifest did not omit
  # the module and that west checked out the exact revision pinned by Zephyr.
  for project in cmsis_6 hal_nordic; do
    module_dir="$(west list "${project}" -f '{abspath}')"
    expected="$(west list "${project}" -f '{revision}')"
    actual="$(git -C "${module_dir}" rev-parse HEAD)"
    if [[ "${actual}" != "${expected}" ]]; then
      echo "West module ${project} revision mismatch: ${actual} != ${expected}" >&2
      exit 1
    fi
  done

  west zephyr-export
  west packages pip --install
  python -m pip install -r "${ROOT_DIR}/requirements-host.txt"
  cd "${WORKSPACE_DIR}/zephyr"
  west sdk install --version 1.0.1 --gnu-toolchains arm-zephyr-eabi
)

cat <<MSG
Setup complete.

Build:
  ${ROOT_DIR}/scripts/build.sh

Flash:
  ${ROOT_DIR}/scripts/flash.sh
MSG
