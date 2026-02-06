#!/usr/bin/env bash
set -euo pipefail

# apply-vendor-patches.sh <circle-stdlib-stage>
#
# Applies lockfile-pinned patches to a lockfile-pinned vendor staging checkout.
# This is intentionally fail-closed: any mismatch should abort the build.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
LOCK="${ROOT}/vendors/circle-stdlib/patches.lock"

die() { echo "ERROR: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

need git
need sha256sum

STAGE="${1:-}"
[[ -n "${STAGE}" ]] || die "usage: $0 <circle-stdlib-stage>"
[[ -d "$STAGE" ]] || die "stage not found: $STAGE"
[[ -f "$LOCK" ]] || die "missing patch lock: $LOCK"

apply_one() {
  local target_path="$1"
  local base_sha="$2"
  local patch_file="$3"
  local patch_sha="$4"

  local target="${STAGE}/${target_path}"
  local patch_abs="${ROOT}/${patch_file}"

  [[ -d "$target" ]] || die "missing target_path in stage: ${target_path}"
  git -C "$target" rev-parse --git-dir >/dev/null 2>&1 || die "target_path is not a git repo: ${target_path}"

  [[ -f "$patch_abs" ]] || die "missing patch file: ${patch_file}"
  echo "${patch_sha}  ${patch_abs}" | sha256sum -c - >/dev/null || die "patch sha256 mismatch: ${patch_file}"

  local head
  head="$(git -C "$target" rev-parse HEAD)"
  [[ "$head" == "$base_sha" ]] || die "base SHA mismatch for ${target_path}: got ${head}, expected ${base_sha}"

  git -C "$target" apply --check "$patch_abs" >/dev/null || die "patch does not apply cleanly: ${patch_file}"
  git -C "$target" apply "$patch_abs" >/dev/null || die "failed to apply patch: ${patch_file}"

  echo "OK: patched ${target_path} <= ${patch_file}" >&2
}

while IFS= read -r line; do
  [[ -n "${line}" ]] || continue
  [[ "${line}" =~ ^# ]] && continue

  # shellcheck disable=SC2086
  set -- $line
  [[ $# -eq 4 ]] || die "invalid patches.lock line (expected 4 fields): $line"
  apply_one "$1" "$2" "$3" "$4"
done < "$LOCK"

