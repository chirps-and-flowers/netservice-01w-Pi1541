#!/usr/bin/env bash
set -euo pipefail

# bootstrap-circle-stdlib.sh
# Cached, lockfile-verified checkout of circle-stdlib (+ submodules) into build/staging.
#
# Rationale:
# - Deterministic builds for a cleaner debug path.
# - Repo stays cleanish (no Circle submodule in the main tree).
#
# Stdout: stage path (for scripts)
# Stderr: progress/errors

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
LOCK="${ROOT}/vendors/circle-stdlib.lock"
SUBLOCK="${ROOT}/vendors/circle-stdlib/submodules.lock"

BUILD_DIR="${ROOT}/build"
STAGE="${BUILD_DIR}/staging/circle-stdlib"

MODE="cached" # cached | clean

die() { echo "ERROR: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

need git
need awk
need cut
need diff
need grep
need mktemp

export GIT_TERMINAL_PROMPT=0

read_lock() {
  local key="$1"
  local file="$2"
  local v
  v="$(grep -E "^${key}=" "$file" | head -n1 | cut -d= -f2-)"
  [[ -n "${v:-}" ]] || die "lockfile missing key: ${key} (${file})"
  echo "$v"
}

submodules_normalized() {
  # Emit "sha path" lines from submodule status output.
  git -C "$STAGE" submodule status --recursive \
    | awk 'NF>=2{sha=$1; path=$2; sub(/^[+-]/,"",sha); print sha, path}'
}

verify_submodules() {
  local tmp expected
  tmp="$(mktemp)"
  expected="$(mktemp)"

  submodules_normalized | sed 's/[[:space:]]\+/ /g' > "$tmp"
  awk 'NF>=2{print $1, $2}' "$SUBLOCK" | sed 's/[[:space:]]\+/ /g' > "$expected"

  if ! diff -u "$expected" "$tmp" >/dev/null; then
    echo "Submodule lock mismatch:" >&2
    diff -u "$expected" "$tmp" >&2 || true
    rm -f "$tmp" "$expected"
    return 1
  fi

  rm -f "$tmp" "$expected"
  return 0
}

checkout_submodules_to_lock() {
  while read -r sha path; do
    [[ -n "${sha:-}" ]] || continue
    [[ -n "${path:-}" ]] || continue
    [[ -d "$STAGE/$path" ]] || die "missing submodule path: $path"

    git -C "$STAGE/$path" cat-file -e "$sha^{commit}" >/dev/null 2>&1 \
      || die "missing commit $sha in $path"

    git -C "$STAGE/$path" checkout -f "$sha" >/dev/null 2>&1 \
      || die "failed to checkout $sha in $path"
    git -C "$STAGE/$path" reset --hard "$sha" >/dev/null 2>&1 \
      || die "failed to reset $sha in $path"
    git -C "$STAGE/$path" clean -fdx >/dev/null 2>&1 \
      || die "failed to clean $path"
  done < <(awk 'NF>=2{print $1, $2}' "$SUBLOCK")
}

clean_checkout() {
  rm -rf "$STAGE"
  mkdir -p "$(dirname "$STAGE")"

  local repo commit
  repo="$(read_lock repo_url "$LOCK")"
  commit="$(read_lock superproject_sha "$LOCK")"

  echo "circle-stdlib: cloning $repo @ $commit" >&2
  git clone "$repo" "$STAGE" >/dev/null 2>&1
  git -C "$STAGE" checkout -f "$commit" >/dev/null 2>&1

  git -C "$STAGE" submodule sync --recursive >/dev/null 2>&1
  git -C "$STAGE" submodule update --init --recursive >/dev/null 2>&1
  checkout_submodules_to_lock
  verify_submodules
}

try_cached_checkout() {
  [[ -d "$STAGE/.git" ]] || return 1

  local commit
  commit="$(read_lock superproject_sha "$LOCK")"

  git -C "$STAGE" checkout -f "$commit" >/dev/null 2>&1 || return 1
  git -C "$STAGE" reset --hard "$commit" >/dev/null 2>&1 || return 1

  # Keep install/ to avoid rebuilding newlib every time.
  git -C "$STAGE" clean -fdx -e install >/dev/null 2>&1 || return 1

  git -C "$STAGE" submodule sync --recursive >/dev/null 2>&1 || return 1
  git -C "$STAGE" submodule update --init --recursive >/dev/null 2>&1 || return 1

  checkout_submodules_to_lock
  verify_submodules
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cached) MODE="cached"; shift ;;
    --clean) MODE="clean"; shift ;;
    -h|--help)
      cat <<EOF
Usage: $0 [--cached|--clean]

Defaults to --cached (reuse build/staging/circle-stdlib if it matches lockfiles).
Prints the stage path to stdout.
EOF
      exit 0
      ;;
    *) die "unknown arg: $1" ;;
  esac
done

if [[ "$MODE" == "clean" ]]; then
  clean_checkout
else
  if try_cached_checkout; then
    echo "circle-stdlib: cached staging accepted" >&2
  else
    echo "circle-stdlib: cached staging rejected; falling back to clean checkout" >&2
    clean_checkout
  fi
fi

printf '%s\n' "$STAGE"
