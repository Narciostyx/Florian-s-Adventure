#!/usr/bin/env python3
"""Check that tracked C++ sources match clang-format output.

Why not compare inside PowerShell: `Out-String` wraps long lines at the console
width, so a 200-column line can look "unformatted" even when it matches. This
script compares bytes with CRLF normalised, which is stable on every runner.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


def tracked_sources() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "*.hpp", "*.cpp"],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line.strip()]


def normalised(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").rstrip()


def main() -> int:
    if shutil.which("clang-format") is None:
        print("clang-format is not on PATH", file=sys.stderr)
        return 2

    failed: list[str] = []
    files = tracked_sources()
    for name in files:
        path = Path(name)
        formatted = subprocess.run(
            ["clang-format", name], capture_output=True
        )
        if formatted.returncode != 0:
            failed.append(f"{name} (clang-format failed)")
            continue
        if normalised(path.read_bytes()) != normalised(formatted.stdout):
            failed.append(name)

    if failed:
        print("needs formatting (run clang-format -i):")
        for name in failed:
            print(f"  {name}")
        return 1

    print(f"format ok: {len(files)} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())