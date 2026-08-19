#!/bin/sh
# Run the config editor's self-test. Rebuilds first so the test always sees the
# current sources. Needs gjs (Debian/Ubuntu: apt install gjs) - there is no
# browser here and `node` is a broken snap on this box.
set -eu
cd "$(dirname "$0")"

if ! command -v gjs >/dev/null 2>&1; then
  echo "test: gjs not found (apt install gjs)" >&2
  exit 1
fi

./build.sh >/dev/null
exec gjs test/selftest.js
