#!/usr/bin/env bash
set -euo pipefail

# bootstrap-style64-font.sh
# Fetch and verify the Style64 C64 Pro Mono TrueType font used by the web UI.
#
# We do not vendor the font in git. The upstream license text discourages
# direct-download hosting; instead we fetch the pinned upstream archive.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
. "${ROOT}/tools/lib-bootstrap.sh"
LOCK_FILE="${ROOT}/vendors/style64-font.lock"
CACHE_DIR="${ROOT}/build/cache/style64-font"
DST_TTF="${ROOT}/src/webcontent/C64_Pro_Mono-STYLE.ttf"

usage() {
  cat <<'EOF'
Usage:
  ./tools/bootstrap-style64-font.sh [--clean]

Effect:
  Ensures src/webcontent/C64_Pro_Mono-STYLE.ttf exists (downloaded + verified).
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
need curl
need sha256sum
need stat
need unzip

SRC_URL="$(lock_get source_url "$LOCK_FILE")"
SRC_SHA="$(lock_get source_sha256 "$LOCK_FILE")"
SRC_BYTES="$(lock_get source_bytes "$LOCK_FILE")"

TTF_RELPATH="$(lock_get ttf_relpath "$LOCK_FILE")"
TTF_SHA="$(lock_get ttf_sha256 "$LOCK_FILE")"
TTF_BYTES="$(lock_get ttf_bytes "$LOCK_FILE")"

mkdir -p "$CACHE_DIR" "$(dirname "$DST_TTF")"

ARCHIVE_PATH="${CACHE_DIR}/$(basename "$SRC_URL")"

tmp_zip=""
tmp_ttf=""
cleanup_tmp() {
  [[ -n "${tmp_zip:-}" ]] && rm -f "$tmp_zip"
  [[ -n "${tmp_ttf:-}" ]] && rm -f "$tmp_ttf"
  return 0
}
trap cleanup_tmp EXIT

if [[ "$CLEAN" -eq 1 ]]; then
  rm -f "${ARCHIVE_PATH:?}"
fi

if [[ -f "$DST_TTF" ]] && verify_file "$DST_TTF" "$TTF_SHA" "$TTF_BYTES"; then
  exit 0
fi

if [[ ! -f "$ARCHIVE_PATH" ]] || ! verify_file "$ARCHIVE_PATH" "$SRC_SHA" "$SRC_BYTES"; then
  log "Downloading Style64 font archive..."
  tmp_zip="${ARCHIVE_PATH}.tmp.$$"
  curl -fsSL "$SRC_URL" -o "$tmp_zip"
  verify_file "$tmp_zip" "$SRC_SHA" "$SRC_BYTES" || die "archive verification failed"
  mv -f "$tmp_zip" "$ARCHIVE_PATH"
  tmp_zip=""
fi

log "Extracting pinned TTF from archive..."
tmp_ttf="${DST_TTF}.tmp.$$"
unzip -p "$ARCHIVE_PATH" "$TTF_RELPATH" >"$tmp_ttf"
verify_file "$tmp_ttf" "$TTF_SHA" "$TTF_BYTES" || die "ttf verification failed"
mv -f "$tmp_ttf" "$DST_TTF"
tmp_ttf=""
