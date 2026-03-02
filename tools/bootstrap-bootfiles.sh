#!/usr/bin/env bash
set -euo pipefail

# bootstrap-bootfiles.sh
# Cached, lockfile-verified fetch of pinned Raspberry Pi boot firmware files.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
. "${ROOT}/tools/lib-bootstrap.sh"
LOCK_FILE="${ROOT}/vendors/bootfiles.lock"
CACHE_DIR="${ROOT}/build/cache/bootfiles"
DOWNLOAD_DIR="${CACHE_DIR}/downloads"
STAGING_DIR="${CACHE_DIR}/staging"

for_each_lock_entry() {
  local callback="$1"
  shift
  local line
  local count=0

  while IFS= read -r line; do
    [[ -z "$line" || "${line:0:1}" == "#" ]] && continue
    [[ "$line" == file\ * ]] || continue

    # bootfiles.lock format: file <path> <sha256> <bytes>
    local kind rel sha size extra
    read -r kind rel sha size extra <<<"$line"
    [[ "$kind" == "file" && -n "${rel:-}" && -n "${sha:-}" && -n "${size:-}" && -z "${extra:-}" ]] \
      || die "invalid lock entry: $line"

    "$callback" "$@" "$rel" "$sha" "$size" || return 1
    count=$((count + 1))
  done < "$LOCK_FILE"

  [[ "$count" -gt 0 ]] || die "bootfiles.lock has no file entries"
}

verify_stage_entry() {
  local stage_root="$1"
  local rel="$2"
  local sha="$3"
  local size="$4"
  verify_file "${stage_root}/${rel}" "$sha" "$size"
}

verify_stage() {
  local stage="$1"
  [[ -d "$stage" ]] || return 1
  for_each_lock_entry verify_stage_entry "$stage"
}

extract_stage_entry() {
  local src_root="$1"
  local tmp_stage="$2"
  local rel="$3"
  local sha="$4"
  local size="$5"

  local src="${src_root}/boot/${rel}"
  [[ -f "$src" ]] || die "missing source file in archive: boot/${rel}"

  mkdir -p "${tmp_stage}/$(dirname "$rel")"
  cp -f "$src" "${tmp_stage}/${rel}"
  verify_file "${tmp_stage}/${rel}" "$sha" "$size" || die "verification failed for ${rel}"
}

extract_to_stage() {
  local archive="$1"
  local stage="$2"
  local tmp_extract
  local src_root
  local tmp_stage

  tmp_extract="$(mktemp -d "${CACHE_DIR}/extract.XXXXXX")"
  tmp_stage="${stage}.tmp.$$"
  trap 'rm -rf "$tmp_extract" "$tmp_stage"' EXIT

  tar -xf "$archive" -C "$tmp_extract"
  src_root="$(find "$tmp_extract" -mindepth 1 -maxdepth 1 -type d | head -n1)"
  [[ -n "${src_root:-}" ]] || die "failed to locate extracted firmware root"

  rm -rf "$tmp_stage"
  mkdir -p "$tmp_stage"

  for_each_lock_entry extract_stage_entry "$src_root" "$tmp_stage"

  rm -rf "$stage"
  mv "$tmp_stage" "$stage"
  trap - EXIT
  rm -rf "$tmp_extract"
}

usage() {
  cat <<'EOF'
Usage:
  ./tools/bootstrap-bootfiles.sh [--clean]

Behavior:
  - Downloads the pinned firmware archive from vendors/bootfiles.lock
  - Extracts lock-listed files into build/cache/bootfiles/staging/<source_commit>/
  - Verifies SHA256 + byte size for every file entry

Output:
  Prints the staging directory path on stdout.
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
need tar
need curl
need sha256sum
need stat

SOURCE_URL="$(lock_get source_url "$LOCK_FILE")"
SOURCE_REF="$(lock_get source_ref "$LOCK_FILE")"
SOURCE_COMMIT="$(lock_get source_commit "$LOCK_FILE")"
SOURCE_ARCHIVE_URL="$(lock_get source_archive_url "$LOCK_FILE")"

[[ "$SOURCE_COMMIT" =~ ^[0-9a-f]{40}$ ]] || die "source_commit must be a 40-char SHA1"

mkdir -p "$DOWNLOAD_DIR" "$STAGING_DIR"
ARCHIVE_PATH="${DOWNLOAD_DIR}/firmware-${SOURCE_COMMIT}.tar.gz"
STAGE_PATH="${STAGING_DIR}/${SOURCE_COMMIT}"

if [[ "$CLEAN" -eq 1 ]]; then
  log "Cleaning cached bootfiles for ${SOURCE_REF} (${SOURCE_COMMIT})"
  rm -rf "${STAGE_PATH:?}" "${ARCHIVE_PATH:?}"
fi

if verify_stage "$STAGE_PATH"; then
  printf '%s\n' "$STAGE_PATH"
  exit 0
fi

if [[ ! -f "$ARCHIVE_PATH" ]]; then
  log "Downloading bootfiles: ${SOURCE_REF} (${SOURCE_COMMIT})"
  log "Source: ${SOURCE_URL}"
  tmp_archive="${ARCHIVE_PATH}.tmp.$$"
  rm -f "$tmp_archive"
  if ! download "$SOURCE_ARCHIVE_URL" "$tmp_archive"; then
    rm -f "$tmp_archive"
    die "failed to download bootfiles archive"
  fi
  mv -f "$tmp_archive" "$ARCHIVE_PATH"
fi

log "Extracting and verifying lock-listed boot files..."
extract_to_stage "$ARCHIVE_PATH" "$STAGE_PATH"

verify_stage "$STAGE_PATH" || die "staging verification failed"
printf '%s\n' "$STAGE_PATH"
