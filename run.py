#!/usr/bin/env python3
"""APM のビルドとテストの入口。

python run.py build [wasm]   build/out/<環境>/ にブリッジのライブラリを作る
python run.py test [wasm]    test/out/<環境>/ でテストを作って走らせる
wasm を付けないときは、走らせた OS の版。wasm のときは、emsdk の場所を環境変数 EMSDK から知る
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent

SCRIPTS = {
    "build": ROOT / "build" / "build.py",
    "test": ROOT / "test" / "test.py",
}


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in SCRIPTS:
        sys.exit("使い方：python run.py build|test [wasm]")
    result = subprocess.run([sys.executable, SCRIPTS[sys.argv[1]], *sys.argv[2:]])
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
