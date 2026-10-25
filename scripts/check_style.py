#!/usr/bin/env python3
"""检查项目自有代码格式；不会改写 third_party、模型或部署产物。"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def tool(name, variable):
    configured = os.environ.get(variable)
    located = configured or shutil.which(name)
    if not located:
        candidate = ROOT / "node_modules" / ".bin" / name
        if candidate.is_file():
            located = str(candidate)
    if not located:
        raise SystemExit(
            f"Missing {name}; see docs/build_and_deploy.md ({variable} can override it)"
        )
    return located


def run(arguments):
    subprocess.run(arguments, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true", help="Apply the configured format")
    args = parser.parse_args()
    sources = [
        path
        for directory in ("apps", "examples", "tests")
        for path in (ROOT / directory).rglob("*")
    ]
    unsupported = [
        str(path.relative_to(ROOT)) for path in sources if path.suffix in {".cc", ".cxx"}
    ]
    if unsupported:
        raise SystemExit("C++ implementation files must use .cpp:\n" + "\n".join(unsupported))
    cpp = sorted(str(path.relative_to(ROOT)) for path in sources if path.suffix in {".cpp", ".h"})
    python = sorted(
        str(path.relative_to(ROOT))
        for directory in ("tests", "scripts")
        for path in (ROOT / directory).rglob("*.py")
    )
    web = [
        "apps/SmartIPC/web/app.js",
        "apps/SmartIPC/web/index.html",
        "apps/SmartIPC/web/style.css",
        ".prettierrc.json",
        "eslint.config.mjs",
        "package.json",
    ]

    clang = tool("clang-format", "NOPISKL_CLANG_FORMAT")
    prettier = tool("prettier", "NOPISKL_PRETTIER")
    ruff = tool("ruff", "NOPISKL_RUFF")
    run([clang, "-i" if args.fix else "--dry-run", *([] if args.fix else ["--Werror"]), *cpp])
    run([prettier, "--write" if args.fix else "--check", *web])
    run([ruff, "format", *([] if args.fix else ["--check"]), *python])
    run([ruff, "check", *python])
    print(f"PASS: {len(cpp)} C/C++ files, {len(web)} web/config files, {len(python)} Python files")


if __name__ == "__main__":
    main()
