#!/bin/sh
# Build index.html from src/. No npm, no bundler: the template is copied
# verbatim except for lines of the form
#
#   <!-- include: some/file -->
#
# which are replaced by the contents of src/some/file. That keeps the shipped
# app a single self-contained file you can open straight off disk (WebSerial
# works from file://, ES modules do not) while the sources stay split.
#
#   ./build.sh          rebuild index.html
#   ./build.sh --check   exit 1 if index.html is stale (for CI / pre-commit)
set -eu
cd "$(dirname "$0")"

TPL=src/index.template.html
OUT=index.html
TMP=$(mktemp) || exit 1
trap 'rm -f "$TMP"' EXIT

LC_ALL=C awk '
  /^[[:space:]]*<!-- include: .* -->[[:space:]]*$/ {
    path = $0
    sub(/^[[:space:]]*<!-- include: /, "", path)
    sub(/ -->[[:space:]]*$/, "", path)
    path = "src/" path
    n = 0
    while ((getline line < path) > 0) { print line; n++ }
    close(path)
    if (n == 0) { print "build: missing or empty " path > "/dev/stderr"; exit 1 }
    next
  }
  { print }
' "$TPL" > "$TMP"

if [ "${1:-}" = "--check" ]; then
  if cmp -s "$TMP" "$OUT"; then
    echo "index.html is up to date"
  else
    echo "index.html is STALE - run ./build.sh" >&2
    exit 1
  fi
else
  cp "$TMP" "$OUT"
  echo "built $OUT ($(wc -l < "$OUT" | tr -d ' ') lines)"
fi
