#!/usr/bin/env bash
set -euo pipefail

# build.sh - Pi Zero 1 W (01W) build entrypoint for this fork.
#
# Default: build both kernels
#   - Legacy emulator kernel (RASPPI=0)
#   - Circle service kernel (Circle RASPPI=1 / Pi1-class, runs on Pi Zero)
#
# This script is intentionally Pi Zero 1 W only. Keep it minimal and deterministic for simpler debugging.
#
# NOTE: "RASPPI" means different things in different parts of this codebase:
# - Pi1541 legacy build: RASPPI=0 selects the Pi Zero / ARMv6 (ARM1176) path.
# - Circle build:        -r 1 / RASPPI=1 targets the Pi 1 class (includes Pi Zero).
#                        Multicore exists only when Circle defines ARM_ALLOW_MULTI_CORE.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"

BUILD_DIR="${ROOT}/build"
OUT_DIR="${BUILD_DIR}/out"

MODE="both"          # both | pi1541 | service
STAGE_MODE="cached"  # cached | clean
TOOLCHAIN_CLEAN=0
ALLOW_DIRTY=0

usage() {
  cat <<'EOF'
Usage:
  ./build.sh [--pi1541] [--service] [--clean|--cached] [--clean-circle] [--clean-toolchain] [--allow-dirty]

Modes:
  (default)   Build both legacy (emu) + Circle (service)
  --pi1541    Build legacy (emu) only
  --service   Build Circle (service) only

Staging:
  --cached           Reuse build/staging if it matches lockfiles (default)
  --clean            Clean Circle staging + toolchain cache (forces toolchain re-download)
  --clean-circle     Clean Circle staging only
  --clean-toolchain  Clean toolchain cache only (forces toolchain re-download)

Safety:
  --allow-dirty   Allow building with local tracked changes (developer only)
EOF
}

die() { echo "ERROR: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }
lock_get() {
  local key="$1"
  local file="$2"
  local v
  v="$(grep -E "^${key}=" "$file" | head -n1 | cut -d= -f2-)"
  [[ -n "${v:-}" ]] || die "lockfile missing key: ${key} (${file})"
  printf '%s\n' "$v"
}

jobs() {
  if command -v nproc >/dev/null 2>&1; then nproc; else getconf _NPROCESSORS_ONLN; fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pi1541) MODE="pi1541"; shift ;;
    --service) MODE="service"; shift ;;
    --clean) STAGE_MODE="clean"; TOOLCHAIN_CLEAN=1; shift ;;
    --clean-circle) STAGE_MODE="clean"; shift ;;
    --clean-toolchain) TOOLCHAIN_CLEAN=1; shift ;;
    --cached) STAGE_MODE="cached"; shift ;;
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown arg: $1" ;;
  esac
done

need git
need make

if [[ "$ALLOW_DIRTY" -eq 0 ]]; then
  dirty="$(git -C "$ROOT" status --porcelain --untracked-files=no)"
  [[ -z "$dirty" ]] || die "repo has uncommitted tracked changes; commit/stash or pass --allow-dirty"
fi

mkdir -p "$OUT_DIR"

# Toolchain (pinned) -----------------------------------------------------------
if [[ "$TOOLCHAIN_CLEAN" -eq 1 ]]; then
  eval "$("${ROOT}/tools/bootstrap-toolchain.sh" --arm32 --clean --print-env)"
else
  eval "$("${ROOT}/tools/bootstrap-toolchain.sh" --arm32 --print-env)"
fi

# Fail closed: ensure we are using the pinned toolchain binary, not whatever is
# present on the developer machine.
gcc_path="$(command -v arm-none-eabi-gcc)"
expected_gcc="${TOOLCHAIN_BIN}/arm-none-eabi-gcc"
[[ "$gcc_path" == "$expected_gcc" ]] || die "arm-none-eabi-gcc is not the pinned toolchain: got ${gcc_path}, expected ${expected_gcc}"

expected_gcc_version="$(lock_get toolchain_human "${ROOT}/vendors/toolchain.lock")"
actual_gcc_version="$(arm-none-eabi-gcc --version | head -n1)"
[[ "$actual_gcc_version" == "$expected_gcc_version" ]] || die "toolchain version mismatch: got '${actual_gcc_version}', expected '${expected_gcc_version}'"

echo "Toolchain: ${actual_gcc_version}" >&2

# Circle service kernel --------------------------------------------------------
build_service() {
  local stage
  stage="$("${ROOT}/tools/bootstrap-circle-stdlib.sh" "--${STAGE_MODE}")"
  [[ -d "$stage" ]] || die "circle-stdlib staging missing: $stage"

  echo "circle-stdlib: applying vendor patches (lockfile pinned)" >&2
  "${ROOT}/tools/apply-vendor-patches.sh" "$stage" >/dev/null

  echo "circle-stdlib: configure (-r 1, arm-none-eabi-)" >&2
  ( cd "$stage" && ./configure -r 1 --prefix arm-none-eabi- ) >/dev/null

  echo "circle-stdlib: build newlib + circle (this can take a while)" >&2
  ( cd "$stage" && make -j"$(jobs)" newlib circle ) >/dev/null

  # The Circle kernel depends on generated webcontent headers (src/webcontent/*.h).
  make -C "${ROOT}/src/webcontent" all >/dev/null

  # Service kernel links an explicit object list (service/*.o + minimal shared objs),
  # so Makefile.circle’s default OBJS (which include emulator objects) are overridden here.
  # NETSERVICE_TARGET_SERVICE selects the service kernel headers via src/circle-types.h.
  local service_circle_objs
  local service_common_objs
  service_circle_objs="service/main.o service/kernel.o"
  service_common_objs="service/service.o service/http_server.o service/shim.o options.o ScreenLCD.o SSD1306.o xga_font_data.o"

  echo "service kernel: building (Circle, Pi Zero)" >&2
  make -C "${ROOT}/src" -f Makefile.circle CIRCLEBASE="$stage" \
    XFLAGS="-DNETSERVICE_TARGET_SERVICE=1" \
    CIRCLE_OBJS="$service_circle_objs" COMMON_OBJS="$service_common_objs" \
    clean >/dev/null
  make -C "${ROOT}/src" -f Makefile.circle CIRCLEBASE="$stage" \
    XFLAGS="-DNETSERVICE_TARGET_SERVICE=1" \
    CIRCLE_OBJS="$service_circle_objs" COMMON_OBJS="$service_common_objs" \
    -j"$(jobs)" >/dev/null

  [[ -f "${ROOT}/src/kernel.img" ]] || die "service kernel missing: ${ROOT}/src/kernel.img"
  cp -f "${ROOT}/src/kernel.img" "${OUT_DIR}/kernel_service.img"
  # Clean up old artifact name(s) from earlier iterations.
  rm -f "${OUT_DIR}/kernel_srv.img"
  echo "OK: ${OUT_DIR}/kernel_service.img" >&2
}

# Legacy emulator kernel -------------------------------------------------------
build_pi1541() {
  echo "emu kernel: building (legacy, RASPPI=0)" >&2
  make RASPPI=0 clean >/dev/null
  make legacy RASPPI=0 -j"$(jobs)" >/dev/null
  [[ -f "${ROOT}/kernel.img" ]] || die "legacy kernel missing: ${ROOT}/kernel.img"
  cp -f "${ROOT}/kernel.img" "${OUT_DIR}/kernel_pi1541.img"
  # Clean up old artifact name(s) from earlier iterations.
  rm -f "${OUT_DIR}/kernel_emu.img"
  echo "OK: ${OUT_DIR}/kernel_pi1541.img" >&2
}

# Legacy chainloader kernel ----------------------------------------------------
build_chainloader() {
  echo "chainloader kernel: building (legacy, RASPPI=0)" >&2
  make chainloader-clean RASPPI=0 >/dev/null
  make chainloader RASPPI=0 -j"$(jobs)" >/dev/null
  [[ -f "${ROOT}/kernel_chainloader.img" ]] || die "chainloader kernel missing: ${ROOT}/kernel_chainloader.img"
  cp -f "${ROOT}/kernel_chainloader.img" "${OUT_DIR}/kernel_chainloader.img"
  echo "OK: ${OUT_DIR}/kernel_chainloader.img" >&2
}

case "$MODE" in
  both)
    build_service
    build_pi1541
    build_chainloader
    ;;
  service)
    build_service
    ;;
  pi1541)
    build_pi1541
    build_chainloader
    ;;
  *)
    die "internal: unknown MODE=$MODE"
    ;;
esac

echo "Artifacts in ${OUT_DIR}:" >&2
for f in kernel_pi1541.img kernel_chainloader.img kernel_service.img; do
  [[ -f "${OUT_DIR}/${f}" ]] || continue
  ls -lh "${OUT_DIR}/${f}" >&2
done
