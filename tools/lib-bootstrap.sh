#!/usr/bin/env bash

# Shared helpers for bootstrap/build scripts.

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

download() {
  local url="$1"
  local out="$2"
  curl -fsSL "$url" -o "$out"
}
