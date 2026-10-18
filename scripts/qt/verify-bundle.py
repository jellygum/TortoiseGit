#!/usr/bin/env python3
"""Reject development-machine library paths in a deployed macOS bundle."""

import re
import subprocess
import sys
from pathlib import Path


def main():
    bundle = Path(sys.argv[1]).resolve()
    if not bundle.is_dir():
        raise SystemExit("App bundle not found: " + str(bundle))
    required = [bundle / "Contents/MacOS" / name
                for name in ("TortoiseGitQt", "TortoiseGitAskPass")]
    if not all(path.is_file() for path in required):
        raise SystemExit("The main executable or askpass helper is missing")
    magic_numbers = {bytes.fromhex(value) for value in
                     ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
    inspected = 0
    failures = []
    for path in bundle.rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as binary:
            if binary.read(4) not in magic_numbers:
                continue
        inspected += 1
        dependencies = subprocess.check_output(["otool", "-L", str(path)], text=True)
        for dependency in re.findall(r"^\s+(.+?) \(compatibility version", dependencies, re.M):
            if not dependency.startswith(("@rpath/", "@loader_path/", "@executable_path/", "/System/Library/", "/usr/lib/")):
                failures.append(f"{path.name}: external dependency {dependency}")
        commands = subprocess.check_output(["otool", "-l", str(path)], text=True)
        for rpath in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", commands):
            if not rpath.startswith(("@loader_path/", "@executable_path/")):
                failures.append(f"{path.name}: external runtime path {rpath}")
    if failures:
        raise SystemExit("\n".join(failures))
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
    print(f"Bundle audit passed: {inspected} Mach-O files; relative/system dependencies and local signature verified")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: verify-bundle.py /path/to/TortoiseGitQt.app")
    main()
