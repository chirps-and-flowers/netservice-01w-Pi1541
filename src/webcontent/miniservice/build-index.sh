#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <template.html> <style.css> <app.js>" >&2
  exit 2
fi

template="$1"
style="$2"
app="$3"

awk -v style="$style" -v app="$app" '
function emit_file(path, compact, line) {
  while ((getline line < path) > 0) {
    if (compact) {
      sub(/^[ \t]+/, "", line)
      sub(/[ \t]+$/, "", line)
      if (line == "") {
        continue
      }
    }
    print line
  }
  close(path)
}
{
  if ($0 == "@@INLINE_STYLE@@") {
    emit_file(style, 1)
    next
  }
  if ($0 == "@@INLINE_APP@@") {
    emit_file(app, 1)
    next
  }
  line = $0
  sub(/^[ \t]+/, "", line)
  sub(/[ \t]+$/, "", line)
  if (line != "") {
    print line
  }
}
' "$template"
