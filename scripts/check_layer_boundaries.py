#!/usr/bin/env python3
from pathlib import Path
import re
import sys


INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    violations = []
    forbidden_prefixes = (
        "niki/l1_",
        "niki/l2_",
        "niki/l3_",
        "niki/l4_",
        "niki/l5_",
        "niki/domain/",
        "niki/meta/",
    )

    adapter_allowlist = {
        Path("src/l0_core/linker/linker.cpp"),
        Path("src/l0_core/runtime/launcher.cpp"),
        Path("include/niki/l0_core/ir/module_ir.hpp"),
    }
    targets = list((repo / "src" / "l0_core").rglob("*.cpp")) + list((repo / "include" / "niki" / "l0_core").rglob("*.hpp"))
    for path in targets:
        if path.relative_to(repo) in adapter_allowlist:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for lineno, line in enumerate(text.splitlines(), start=1):
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            header = m.group(1)
            if header.startswith(forbidden_prefixes):
                violations.append(f"{path.relative_to(repo)}:{lineno}: forbidden include '{header}'")

    if violations:
        print("Layer boundary violations found:")
        for v in violations:
            print(v)
        return 1

    meta_targets = list((repo / "src" / "meta").rglob("*.cpp")) + list((repo / "include" / "niki" / "meta").rglob("*.hpp"))
    for path in meta_targets:
        rel = path.relative_to(repo)
        text = path.read_text(encoding="utf-8", errors="ignore")
        for lineno, line in enumerate(text.splitlines(), start=1):
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            header = m.group(1)
            if "meta/precompile/" in str(rel).replace("\\", "/"):
                if header.startswith("niki/meta/orchestrator/"):
                    violations.append(f"{rel}:{lineno}: precompile must not include '{header}'")
            if "meta/project/" in str(rel).replace("\\", "/") or "meta/runtime_host/" in str(rel).replace("\\", "/"):
                if header.startswith("niki/meta/orchestrator/") or header.startswith("niki/meta/precompile/"):
                    violations.append(f"{rel}:{lineno}: project/runtime_host must not include '{header}'")

    if violations:
        print("Layer boundary violations found:")
        for v in violations:
            print(v)
        return 1

    print("Layer boundary check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
