#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
CACHE_DIR="${ROOT}/build/cache"
TOOLCHAIN_DIR="${CACHE_DIR}/toolchains"

# ARM32 toolchain (required for Pi Zero/legacy)
TOOLCHAIN_VERSION="14.2.1-1.1"
TOOLCHAIN_TARBALL_SHA256="ed8c7d207a85d00da22b90cf80ab3b0b2c7600509afadf6b7149644e9d4790a6"
TOOLCHAIN_BASE="xpack-arm-none-eabi-gcc-${TOOLCHAIN_VERSION}"
TOOLCHAIN_TARBALL="${TOOLCHAIN_BASE}-linux-x64.tar.gz"
TOOLCHAIN_URL="https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${TOOLCHAIN_VERSION}/${TOOLCHAIN_TARBALL}"
TOOLCHAIN_BIN="${TOOLCHAIN_DIR}/${TOOLCHAIN_BASE}/bin"

export TOOLCHAIN_VERSION TOOLCHAIN_TARBALL_SHA256 TOOLCHAIN_URL TOOLCHAIN_BIN

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing command: $1" >&2; exit 1; }; }
log() { [ "${PRINT_ENV:-0}" -eq 1 ] && return 0; echo "$@" >&2; }

fetch() {
  local url="$1"
  local out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$out"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$out" "$url"
  else
    echo "missing downloader: curl or wget" >&2
    exit 1
  fi
}

install_arm32() {
  mkdir -p "$TOOLCHAIN_DIR"
  local tarball_path="${TOOLCHAIN_DIR}/${TOOLCHAIN_TARBALL}"
  if [ ! -x "${TOOLCHAIN_BIN}/arm-none-eabi-gcc" ]; then
    log "Downloading ARM32 toolchain ${TOOLCHAIN_VERSION}..."
    fetch "$TOOLCHAIN_URL" "$tarball_path"
    echo "${TOOLCHAIN_TARBALL_SHA256}  ${tarball_path}" | sha256sum -c - >/dev/null
    tar -xf "$tarball_path" -C "$TOOLCHAIN_DIR"
  fi
  export PATH="${TOOLCHAIN_BIN}:$PATH"
}

ARM32=1
PRINT_ENV=0
while [ $# -gt 0 ]; do
  case "$1" in
    --arm32) ARM32=1; shift ;;
    --print-env) PRINT_ENV=1; shift ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

need sha256sum
need tar

if [ "$ARM32" -eq 1 ]; then
  install_arm32
fi

if [ "$PRINT_ENV" -eq 1 ]; then
  # Emit exports for the caller to eval
  if [ "$ARM32" -eq 1 ]; then
    printf 'export PATH="%s:$PATH"\n' "$TOOLCHAIN_BIN"
    printf 'export TOOLCHAIN_VERSION=%s\n' "$TOOLCHAIN_VERSION"
    printf 'export TOOLCHAIN_TARBALL_SHA256=%s\n' "$TOOLCHAIN_TARBALL_SHA256"
    printf 'export TOOLCHAIN_URL=%s\n' "$TOOLCHAIN_URL"
    printf 'export TOOLCHAIN_BIN=%s\n' "$TOOLCHAIN_BIN"
  fi
fi
