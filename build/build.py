#!/usr/bin/env python3
"""build/ の CMake で、APM のブリッジを build/out/<環境>/ に作る。run.py の build から呼ぶ。

引数なしは走らせた OS の版（Linux は libapm.so、Windows は apm.dll と apm.lib）、wasm は WebAssembly の版（apm.js と apm.wasm）
"""
import sys
from pathlib import Path

BUILD_DIR = Path(__file__).resolve().parent

# common/ の common.py を読むため
sys.path.insert(0, str(BUILD_DIR.parent / "common"))
import common  # noqa: E402


def main():
    target = common.parse_target("使い方：python run.py build [wasm]")
    platform, env, args = common.prepare(target)
    out = BUILD_DIR / "out" / platform
    common.configure_and_build(BUILD_DIR, out, env, args)
    print(f"{out} に作りました")


if __name__ == "__main__":
    main()
