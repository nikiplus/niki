#!/usr/bin/env python3
"""Run NIKI on a project directory and assert the process exit code."""
from __future__ import annotations

import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("Usage: run_case.py <niki_exe> <case_dir> <expected_exit>", file=sys.stderr)
        return 2

    niki_exe = sys.argv[1]
    case_dir = sys.argv[2]
    try:
        expected_exit = int(sys.argv[3])
    except ValueError:
        print(f"Invalid expected exit code: {sys.argv[3]!r}", file=sys.stderr)
        return 2

    completed = subprocess.run([niki_exe, case_dir], check=False)
    if completed.returncode != expected_exit:
        print(
            f"FAIL: {case_dir}: expected exit {expected_exit}, got {completed.returncode}",
            file=sys.stderr,
        )
        return 1

    print(f"PASS: {case_dir} (exit {expected_exit})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
