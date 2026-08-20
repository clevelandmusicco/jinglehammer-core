#!/usr/bin/env python3
"""Run the config editor's self-test. Rebuilds first so the test always sees
the current sources. Needs gjs (Debian/Ubuntu: apt install gjs; macOS: brew
install gjs; Windows: use WSL) - there is no browser here and `node` is a
broken snap on this box.
"""
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    if shutil.which("gjs") is None:
        print("test: gjs not found (apt install gjs)", file=sys.stderr)
        sys.exit(1)

    subprocess.run(
        [sys.executable, str(HERE / "build.py")],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    sys.exit(subprocess.run(["gjs", str(HERE / "test" / "selftest.js")]).returncode)


if __name__ == "__main__":
    main()
