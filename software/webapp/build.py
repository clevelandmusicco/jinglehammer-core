#!/usr/bin/env python3
"""Build index.html from src/. No npm, no bundler: the template is copied
verbatim except for lines of the form

  <!-- include: some/file -->

which are replaced by the contents of src/some/file. That keeps the shipped
app a single self-contained file you can open straight off disk (WebSerial
works from file://, ES modules do not) while the sources stay split.

  ./build.py           rebuild index.html
  ./build.py --check   exit 1 if index.html is stale (for CI / pre-commit)

Stdlib only, runs the same on Linux/macOS/Windows.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TPL = HERE / "src" / "index.template.html"
OUT = HERE / "index.html"

INCLUDE_RE = re.compile(r"^[ \t]*<!-- include: (.*?) -->[ \t]*$")


def build():
    out_lines = []
    for line in TPL.read_text(encoding="utf-8").splitlines():
        m = INCLUDE_RE.match(line)
        if not m:
            out_lines.append(line)
            continue
        rel = m.group(1)
        inc = HERE / "src" / rel
        try:
            inc_lines = inc.read_text(encoding="utf-8").splitlines()
        except FileNotFoundError:
            inc_lines = []
        if not inc_lines:
            print(f"build: missing or empty src/{rel}", file=sys.stderr)
            sys.exit(1)
        out_lines.extend(inc_lines)
    return "\n".join(out_lines) + "\n"


def main():
    check = "--check" in sys.argv[1:]
    content = build()
    if check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else None
        if current == content:
            print("index.html is up to date")
        else:
            print("index.html is STALE - run ./build.py", file=sys.stderr)
            sys.exit(1)
    else:
        OUT.write_text(content, encoding="utf-8")
        print(f"built {OUT.name} ({content.count(chr(10))} lines)")


if __name__ == "__main__":
    main()
