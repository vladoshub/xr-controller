#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/xiao_nrf54l15}"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/dist}"

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

for file in zephyr.elf zephyr.hex zephyr.bin zephyr.map zephyr.dts .config; do
  src="${BUILD_DIR}/zephyr/${file}"
  if [[ -f "${src}" ]]; then
    dst_name="${file#.}"
    cp "${src}" "${OUT_DIR}/${dst_name}"
  fi
done

cp "${ROOT_DIR}/README.md" "${OUT_DIR}/README.md"
cp "${ROOT_DIR}/VERSION" "${OUT_DIR}/VERSION"
cp "${ROOT_DIR}/docs/xr_imu_v1.md" "${OUT_DIR}/xr_imu_v1.md"
cp "${ROOT_DIR}/config/capture_service_cpp.yaml" "${OUT_DIR}/capture_service_cpp.yaml"
(
  cd "${OUT_DIR}"
  sha256sum ./* > SHA256SUMS
)
