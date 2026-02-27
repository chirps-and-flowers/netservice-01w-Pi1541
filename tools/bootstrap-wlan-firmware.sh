#!/usr/bin/env bash
set -euo pipefail

# bootstrap-wlan-firmware.sh
# Cached, lockfile-verified fetch of Pi Zero W WLAN firmware files.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
LOCK_FILE="${ROOT}/vendors/wlan-firmware.lock"
CACHE_DIR="${ROOT}/build/cache/wlan-firmware"
STAGE_DIR="${CACHE_DIR}/staging"

die() { echo "ERROR: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }
log() { echo "$@" >&2; }

lock_get() {
  local key="$1"
  local file="$2"
  local v
  v="$(grep -E "^${key}=" "$file" | head -n1 | cut -d= -f2-)"
  [[ -n "${v:-}" ]] || die "lockfile missing key: ${key} (${file})"
  printf '%s\n' "$v"
}

download() {
  local url="$1"
  local out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$out"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$out" "$url"
  else
    die "missing downloader: curl or wget"
  fi
}

verify_file() {
  local path="$1"
  local expect_sha="$2"
  local expect_size="$3"
  local actual_sha
  local actual_size

  if [[ ! -f "$path" ]]; then
    log "verify failed: missing file: ${path}"
    return 1
  fi
  actual_sha="$(sha256sum "$path" | awk '{print $1}')"
  if [[ "$actual_sha" != "$expect_sha" ]]; then
    log "verify failed: sha256 mismatch for ${path}"
    log "  expected: ${expect_sha}"
    log "  actual:   ${actual_sha}"
    return 1
  fi
  actual_size="$(stat -c '%s' "$path")"
  if [[ "$actual_size" != "$expect_size" ]]; then
    log "verify failed: size mismatch for ${path}"
    log "  expected: ${expect_size}"
    log "  actual:   ${actual_size}"
    return 1
  fi
}

for_each_lock_entry() {
  local callback="$1"
  shift
  local line
  local count=0

  while IFS= read -r line; do
    [[ -z "$line" || "${line:0:1}" == "#" ]] && continue
    [[ "$line" == file\ * ]] || continue

    # wlan-firmware.lock format: file <dest> <source_rel> <sha256> <bytes>
    local kind dest rel sha size extra
    read -r kind dest rel sha size extra <<<"$line"
    [[ "$kind" == "file" && -n "${dest:-}" && -n "${rel:-}" && -n "${sha:-}" && -n "${size:-}" && -z "${extra:-}" ]] \
      || die "invalid lock entry: $line"

    "$callback" "$@" "$dest" "$rel" "$sha" "$size" || return 1
    count=$((count + 1))
  done < "$LOCK_FILE"

  [[ "$count" -gt 0 ]] || die "wlan-firmware.lock has no file entries"
}

verify_stage_entry() {
  local stage="$1"
  local dest="$2"
  local rel="$3"
  local sha="$4"
  local size="$5"
  : "$rel"
  verify_file "${stage}/${dest}" "$sha" "$size"
}

verify_stage() {
  local stage="$1"
  [[ -d "$stage" ]] || return 1
  for_each_lock_entry verify_stage_entry "$stage"
}

CURRENT_TMP=""
cleanup_current_tmp() {
  if [[ -n "${CURRENT_TMP:-}" ]]; then
    rm -f "${CURRENT_TMP}"
  fi
  return 0
}
trap cleanup_current_tmp EXIT

download_stage_entry() {
  local stage="$1"
  local base_url="$2"
  local dest="$3"
  local rel="$4"
  local sha="$5"
  local size="$6"
  local final_path tmp

  final_path="${stage}/${dest}"
  mkdir -p "$(dirname "$final_path")"
  tmp="${final_path}.tmp.$$"

  CURRENT_TMP="$tmp"
  download "${base_url}/${rel}" "$tmp"
  verify_file "$tmp" "$sha" "$size" || die "verification failed for ${dest}"
  mv -f "$tmp" "$final_path"
  CURRENT_TMP=""
}

usage() {
  cat <<'EOF'
Usage:
  ./tools/bootstrap-wlan-firmware.sh [--clean]

Output:
  Prints staging directory path on stdout.
EOF
}

CLEAN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean) CLEAN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown arg: $1" ;;
  esac
done

[[ -f "$LOCK_FILE" ]] || die "missing lock file: $LOCK_FILE"
need sha256sum
need stat

SOURCE_REF="$(lock_get source_ref "$LOCK_FILE")"
SOURCE_BASE_URL="$(lock_get source_base_url "$LOCK_FILE")"

mkdir -p "$STAGE_DIR"
STAGE_PATH="${STAGE_DIR}/${SOURCE_REF}"

if [[ "$CLEAN" -eq 1 ]]; then
  rm -rf "${STAGE_PATH:?}"
fi

if verify_stage "$STAGE_PATH"; then
  printf '%s\n' "$STAGE_PATH"
  exit 0
fi

mkdir -p "$STAGE_PATH"

for_each_lock_entry download_stage_entry "$STAGE_PATH" "$SOURCE_BASE_URL"

verify_stage "$STAGE_PATH" || die "staging verification failed"
printf '%s\n' "$STAGE_PATH"
