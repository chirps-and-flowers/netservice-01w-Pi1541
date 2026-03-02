#!/usr/bin/env bash
set -euo pipefail

# build-sd-root.sh
# Assemble the canonical Pi Zero W (01W) netservice SD root in ./build/sd-root.
#
# Contract:
# - Boot firmware (from bootstrap stage, lockfile-verified by vendors/bootfiles.lock):
#     bootcode.bin, start.elf, fixup.dat, bcm2708-rpi-zero-w.dtb
# - WLAN firmware (from bootstrap stage, lockfile-verified by vendors/wlan-firmware.lock):
#     firmware/brcmfmac43430-sdio.{bin,txt,clm_blob}
# - Kernels (from build/out):
#     kernel.img, kernel_chainloader.img, kernel_srv.img, kernel_srv.lz4
# - Config:
#     config.txt, options.txt, wpa_supplicant.conf
# - Runtime data (with-roms mode):
#     chargen, d1541II, dos1541, dos1541ii, dos1581, 1581-rom.318045-02.bin
# - Runtime data (without-roms mode):
#     ROMS_REQUIRED.txt (required ROM filenames + expected hashes/sizes)
# - Runtime dirs:
#     1541/, 1541/_incoming/, 1541/_active_mount/, 1541/_temp_dirty_disks/

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
. "${ROOT}/tools/lib-bootstrap.sh"
OUT_DIR="${ROOT}/build/out"
SD_ROOT="${ROOT}/build/sd-root"
ROM_LOCK="${ROOT}/vendors/pi1541-roms.lock"

usage() {
  cat <<'EOF'
Usage:
  ./tools/build-sd-root.sh [--clean] [--with-roms|--without-roms] [--rom-source=<dir>] [--wpa=<file>]

Behavior:
  - builds a complete manual-copy SD root in build/sd-root
  - installs canonical kernel names from build/out
  - copies lockfile-verified boot firmware from bootstrap stage output
  - copies lockfile-verified wlan firmware from bootstrap stage output
  - copies config.txt and options.txt from repo root
  - writes wpa_supplicant.conf (default: from wpa_supplicant.conf.example)
  - --with-roms: requires runtime ROM/data files that match vendors/pi1541-roms.lock
    (source from repo root or --rom-source directory)
  - --without-roms (default): omits ROM binaries and writes ROMS_REQUIRED.txt for release packaging

Output:
  Prints build/sd-root path on stdout.

Examples:
  # Release-style SD root (no ROM binaries included)
  ./tools/build-sd-root.sh --clean --without-roms

  # Full SD root with ROM/data files from external directory
  ./tools/build-sd-root.sh --clean --with-roms --rom-source=/path/to/roms
EOF
}

for_each_rom_entry() {
  local callback="$1"
  shift
  local line
  local count=0

  while IFS= read -r line; do
    [[ -z "$line" || "${line:0:1}" == "#" ]] && continue
    [[ "$line" == file\ * ]] || continue

    # pi1541-roms.lock format: file <dest_name> <sha256> <bytes>
    local kind name sha size extra
    read -r kind name sha size extra <<<"$line"
    [[ "$kind" == "file" && -n "${name:-}" && -n "${sha:-}" && -n "${size:-}" && -z "${extra:-}" ]] \
      || die "invalid $(basename "$ROM_LOCK") line: $line"

    "$callback" "$@" "$name" "$sha" "$size" || return 1
    count=$((count + 1))
  done < "$ROM_LOCK"

  [[ "$count" -gt 0 ]] || die "$(basename "$ROM_LOCK") has no file entries"
}

copy_rom_entry() {
  local root="$1"
  local rom_source="$2"
  local sd_root="$3"
  local name="$4"
  local sha="$5"
  local size="$6"
  local src=""

  if [[ -f "${root}/${name}" ]]; then
    src="${root}/${name}"
  elif [[ -n "$rom_source" && -f "${rom_source}/${name}" ]]; then
    src="${rom_source}/${name}"
  else
    die "missing runtime file: ${name} (expected sha=${sha}, size=${size})"
  fi

  mkdir -p "$(dirname "${sd_root}/${name}")"
  cp -f "$src" "${sd_root}/${name}" || die "failed to copy runtime file: ${name}"
  verify_file "${sd_root}/${name}" "$sha" "$size" \
    || die "runtime file mismatch: ${name} (expected sha=${sha}, size=${size})"
}

emit_rom_manifest_entry() {
  local name="$1"
  local sha="$2"
  local size="$3"
  echo "${name} ${sha} ${size}"
}

CLEAN=0
INCLUDE_ROMS=0
ROM_SOURCE=""
WPA_SOURCE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean) CLEAN=1; shift ;;
    --with-roms) INCLUDE_ROMS=1; shift ;;
    --without-roms) INCLUDE_ROMS=0; shift ;;
    --rom-source=*) ROM_SOURCE="${1#*=}"; shift ;;
    --wpa=*) WPA_SOURCE="${1#*=}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown arg: $1" ;;
  esac
done

[[ -f "${OUT_DIR}/kernel_pi1541.img" ]] || die "missing ${OUT_DIR}/kernel_pi1541.img (run ./build.sh first)"
[[ -f "${OUT_DIR}/kernel_service.img" ]] || die "missing ${OUT_DIR}/kernel_service.img (run ./build.sh first)"
[[ -f "${OUT_DIR}/kernel_service.lz4" ]] || die "missing ${OUT_DIR}/kernel_service.lz4 (run ./build.sh first)"
[[ -f "${OUT_DIR}/kernel_chainloader.img" ]] || die "missing ${OUT_DIR}/kernel_chainloader.img (run ./build.sh first)"
[[ -f "${ROOT}/config.txt" ]] || die "missing ${ROOT}/config.txt"
[[ -f "${ROOT}/options.txt" ]] || die "missing ${ROOT}/options.txt"
[[ -f "$ROM_LOCK" ]] || die "missing ${ROM_LOCK}"

if [[ -n "$ROM_SOURCE" ]]; then
  [[ -d "$ROM_SOURCE" ]] || die "--rom-source is not a directory: ${ROM_SOURCE}"
fi

if [[ -n "$WPA_SOURCE" ]]; then
  [[ -f "$WPA_SOURCE" ]] || die "--wpa file not found: ${WPA_SOURCE}"
fi

if [[ "$CLEAN" -eq 1 ]]; then
  rm -rf "${SD_ROOT:?}"
fi
mkdir -p "$SD_ROOT"

# Canonical kernel set.
cp -f "${OUT_DIR}/kernel_pi1541.img" "${SD_ROOT}/kernel.img"
cp -f "${OUT_DIR}/kernel_service.img" "${SD_ROOT}/kernel_srv.img"
cp -f "${OUT_DIR}/kernel_service.lz4" "${SD_ROOT}/kernel_srv.lz4"
cp -f "${OUT_DIR}/kernel_chainloader.img" "${SD_ROOT}/kernel_chainloader.img"
# Remove legacy alias from older deploys; canonical name is kernel_srv.lz4.
rm -f "${SD_ROOT}/kernel_srv.img.lz4"

# Canonical config set.
cp -f "${ROOT}/config.txt" "${SD_ROOT}/config.txt"
cp -f "${ROOT}/options.txt" "${SD_ROOT}/options.txt"
if [[ -f "${ROOT}/options.txt.example" ]]; then
  cp -f "${ROOT}/options.txt.example" "${SD_ROOT}/options.txt.example"
fi

if [[ -n "$WPA_SOURCE" ]]; then
  cp -f "$WPA_SOURCE" "${SD_ROOT}/wpa_supplicant.conf"
elif [[ -f "${ROOT}/wpa_supplicant.conf.example" ]]; then
  cp -f "${ROOT}/wpa_supplicant.conf.example" "${SD_ROOT}/wpa_supplicant.conf"
  log "INFO: copied wpa_supplicant.conf.example as wpa_supplicant.conf"
else
  die "missing ${ROOT}/wpa_supplicant.conf.example"
fi

# Boot firmware from lockfile-verified bootstrap stage.
BOOT_STAGE="$("${ROOT}/tools/bootstrap-bootfiles.sh")" || die "bootstrap-bootfiles.sh failed"
[[ -d "$BOOT_STAGE" ]] || die "invalid bootfiles stage: ${BOOT_STAGE}"
cp -a "${BOOT_STAGE}/." "${SD_ROOT}/"

# WLAN firmware from lockfile-verified bootstrap stage.
WLAN_STAGE="$("${ROOT}/tools/bootstrap-wlan-firmware.sh")" || die "bootstrap-wlan-firmware.sh failed"
[[ -d "$WLAN_STAGE" ]] || die "invalid wlan firmware stage: ${WLAN_STAGE}"
mkdir -p "${SD_ROOT}/firmware"
cp -a "${WLAN_STAGE}/." "${SD_ROOT}/firmware/"

if [[ "$INCLUDE_ROMS" -eq 1 ]]; then
  # Required runtime ROM/data set (lockfile pinned).
  for_each_rom_entry copy_rom_entry "$ROOT" "$ROM_SOURCE" "$SD_ROOT"
else
  # Public release mode: include required ROM manifest only.
  {
    echo "This package does not include ROM/data files."
    echo ""
    echo "Minimum required for Pi1541 1541 emulation:"
    echo "- Provide ONE valid 1541 ROM in SD root with one of these names:"
    echo "  d1541.rom"
    echo "  dos1541"
    echo "  d1541II"
    echo "  Jiffy.bin"
    echo ""
    echo "Optional (feature-specific):"
    echo "- dos1581 (or set ROM1581 in options.txt) for D81/1581 emulation"
    echo "- chargen for 8-bit OLED/CBM font rendering (often shipped as c64.bin)"
    echo "  (if your file is c64.bin, rename/copy it to chargen)"
    echo "- Additional 1541 ROMs (for ROM switching via options/browser)"
    echo ""
    echo "For full compatibility with this build, see vendors/pi1541-roms.lock"
    echo "(format: name sha256 size)."
    echo ""
    echo "Format: <name> <sha256> <bytes>"
    for_each_rom_entry emit_rom_manifest_entry
  } > "${SD_ROOT}/ROMS_REQUIRED.txt"
fi

# Precreate runtime directories expected by service + emulator flows.
mkdir -p \
  "${SD_ROOT}/1541" \
  "${SD_ROOT}/1541/_incoming" \
  "${SD_ROOT}/1541/_active_mount" \
  "${SD_ROOT}/1541/_temp_dirty_disks"

printf '%s\n' "$SD_ROOT"
