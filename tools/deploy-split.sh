#!/usr/bin/env bash
set -euo pipefail

# Deploy split artifacts to the Pi1541 SD boot partition.
#
# Scope:
# - install the built kernels:
#     build/out/kernel_pi1541.img  -> SD:/kernel.img
#     build/out/kernel_service.img -> SD:/kernel_srv.img
#     build/out/kernel_chainloader.img  -> SD:/kernel_chainloader.img
# - optionally select which kernel [pi0] boots via SD:/config.txt
#
# Usage:
#   ./tools/deploy-split.sh <sd_mount_point> [--boot=emu|service]
#
# Examples:
#   ./tools/deploy-split.sh /run/media/$USER/PI1541 --boot=service
#   ./tools/deploy-split.sh /run/media/$USER/PI1541 --boot=emu

SD_MOUNT="${1:-}"
BOOT_MODE="emu"

if [[ -z "${SD_MOUNT}" ]]; then
  echo "Usage: $0 <sd_mount_point> [--boot=emu|service]" >&2
  exit 2
fi

shift || true
for arg in "$@"; do
  case "$arg" in
    --boot=emu) BOOT_MODE="emu" ;;
    --boot=service) BOOT_MODE="service" ;;
    *) echo "ERROR: unknown arg: ${arg}" >&2; exit 2 ;;
  esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
OUT_DIR="${ROOT}/build/out"

EMU_SRC="${OUT_DIR}/kernel_pi1541.img"
SRV_SRC="${OUT_DIR}/kernel_service.img"
CHAINLOADER_SRC="${OUT_DIR}/kernel_chainloader.img"

EMU_DEST="${SD_MOUNT}/kernel.img"
SRV_DEST="${SD_MOUNT}/kernel_srv.img"
CHAINLOADER_DEST="${SD_MOUNT}/kernel_chainloader.img"
SRV_LZ4_DEST="${SD_MOUNT}/kernel_srv.lz4"
SRV_IMG_LZ4_DEST="${SD_MOUNT}/kernel_srv.img.lz4"
CFG_DEST="${SD_MOUNT}/config.txt"

[[ -d "${SD_MOUNT}" ]] || { echo "ERROR: SD mount point not found: ${SD_MOUNT}" >&2; exit 1; }
[[ -f "${EMU_SRC}" ]] || { echo "ERROR: missing emu kernel: ${EMU_SRC}" >&2; exit 1; }
[[ -f "${SRV_SRC}" ]] || { echo "ERROR: missing service kernel: ${SRV_SRC}" >&2; exit 1; }
[[ -f "${CHAINLOADER_SRC}" ]] || { echo "ERROR: missing chainloader kernel: ${CHAINLOADER_SRC}" >&2; exit 1; }
CFG_BASE="${CFG_DEST}"
if [[ ! -f "${CFG_DEST}" ]]; then
  # Recover from a partial deploy (or user customizations) by using the newest backup as base.
  CFG_BASE="$(ls -1t "${SD_MOUNT}/config.txt.bak."* 2>/dev/null | head -n1 || true)"
  [[ -n "${CFG_BASE}" ]] || { echo "ERROR: missing config.txt on SD and no config.txt.bak.* found" >&2; exit 1; }
  echo "WARN: config.txt missing; using base: ${CFG_BASE}" >&2
fi

TS="$(date +%Y%m%d-%H%M%S)"

backup_file() {
  local path="$1"
  if [[ -f "$path" ]]; then
    mv -f "$path" "${path}.bak.${TS}"
  fi
}

echo "== Deploy split kernels to ${SD_MOUNT} ==" >&2

backup_file "${EMU_DEST}"
backup_file "${SRV_DEST}"
backup_file "${CHAINLOADER_DEST}"

cp -f "${EMU_SRC}" "${EMU_DEST}"
cp -f "${SRV_SRC}" "${SRV_DEST}"
cp -f "${CHAINLOADER_SRC}" "${CHAINLOADER_DEST}"

# Chainloader prefers .lz4 when present; remove stale service lz4 variants so
# the freshly deployed kernel_srv.img is guaranteed to boot.
rm -f "${SRV_LZ4_DEST}" "${SRV_IMG_LZ4_DEST}"

echo "Installed:" >&2
echo "  emu     -> ${EMU_DEST} ($(stat -c %s "${EMU_DEST}") bytes)" >&2
echo "  service -> ${SRV_DEST} ($(stat -c %s "${SRV_DEST}") bytes)" >&2
echo "  chainld -> ${CHAINLOADER_DEST} ($(stat -c %s "${CHAINLOADER_DEST}") bytes)" >&2
echo "  cleanup -> removed stale ${SRV_LZ4_DEST##*/} / ${SRV_IMG_LZ4_DEST##*/} if present" >&2

echo "Boot mode: ${BOOT_MODE}" >&2

# For config.txt we want a copy, not a move (so we can always read/patch the base).
if [[ -f "${CFG_DEST}" ]]; then
  cp -f "${CFG_DEST}" "${CFG_DEST}.bak.${TS}"
fi

python3 - <<PY
import pathlib

cfg_base = pathlib.Path("${CFG_BASE}")
cfg_dest = pathlib.Path("${CFG_DEST}")
text = cfg_base.read_text(encoding="utf-8", errors="replace").splitlines(True)

mode = "${BOOT_MODE}"
want_kernel = "kernel.img" if mode == "emu" else "kernel_srv.img"
want_addr = "0x1f00000" if mode == "emu" else "0x8000"

out = []
in_pi0 = False
seen_addr = False
seen_kernel = False

def is_section(line: str) -> bool:
    s = line.strip()
    return s.startswith("[") and s.endswith("]")

for line in text:
    if is_section(line):
        # Leaving [pi0] section: inject missing keys before the next section.
        if in_pi0:
            if not seen_addr:
                out.append(f"kernel_address={want_addr}\\n")
            if not seen_kernel:
                out.append(f"kernel={want_kernel}\\n")
        in_pi0 = (line.strip() == "[pi0]")
        seen_addr = False
        seen_kernel = False
        out.append(line)
        continue

    if in_pi0:
        stripped = line.lstrip()
        if stripped.startswith("kernel_address="):
            out.append(f"kernel_address={want_addr}\\n")
            seen_addr = True
            continue
        if stripped.startswith("kernel="):
            out.append(f"kernel={want_kernel}\\n")
            seen_kernel = True
            continue

    out.append(line)

# End of file: if [pi0] was the last section, inject missing keys.
if in_pi0:
    if not seen_addr:
        out.append(f"kernel_address={want_addr}\\n")
    if not seen_kernel:
        out.append(f"kernel={want_kernel}\\n")

cfg_dest.write_text("".join(out), encoding="utf-8")
PY

sync
echo "== Done ==" >&2
