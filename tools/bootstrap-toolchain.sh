#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
CACHE_DIR="${ROOT}/build/cache"
TOOLCHAIN_DIR="${CACHE_DIR}/toolchains"

lock_get() {
  local key="$1"
  local file="$2"
  local v
  v="$(grep -E "^${key}=" "$file" | head -n1 | cut -d= -f2-)"
  [ -n "${v:-}" ] || { echo "lockfile missing key: ${key} (${file})" >&2; exit 1; }
  printf '%s\n' "$v"
}

# ARM32 toolchain (required for Pi Zero/legacy)
LOCK_FILE="${ROOT}/vendors/toolchain.lock"
TOOLCHAIN_VERSION="$(lock_get toolchain_version "${LOCK_FILE}")"
TOOLCHAIN_TARBALL_SHA256="$(lock_get toolchain_tarball_sha256 "${LOCK_FILE}")"
TOOLCHAIN_BASE="arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-arm-none-eabi"
TOOLCHAIN_TARBALL="${TOOLCHAIN_BASE}.tar.xz"
TOOLCHAIN_URL="https://developer.arm.com/-/media/Files/downloads/gnu/${TOOLCHAIN_VERSION}/binrel/${TOOLCHAIN_TARBALL}"
TOOLCHAIN_BIN="${TOOLCHAIN_DIR}/${TOOLCHAIN_BASE}/bin"

export TOOLCHAIN_VERSION TOOLCHAIN_TARBALL_SHA256 TOOLCHAIN_URL TOOLCHAIN_BIN

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing command: $1" >&2; exit 1; }; }
log() { echo "$@" >&2; }

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
  if [ "${CLEAN:-0}" -eq 1 ]; then
    log "Cleaning ARM32 toolchain ${TOOLCHAIN_VERSION}..."
    rm -rf "${TOOLCHAIN_DIR:?}/${TOOLCHAIN_BASE}" "${tarball_path}" || true
  fi
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
CLEAN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --arm32) ARM32=1; shift ;;
    --clean) CLEAN=1; shift ;;
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
