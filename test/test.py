#!/usr/bin/env python3
"""test/ の CMake を test/out/<環境>/ で走らせて Google のテストとブリッジのテストを作り、ctest で走らせる。run.py の test から呼ぶ。
Google のテストの実行ファイルは src/out/<環境>/ に置かれる。

引数なしは走らせた OS の版、wasm は WebAssembly の版（emsdk の node で走らせる）
"""
import os
import sys
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent

# common/ の common.py を読むため
sys.path.insert(0, str(TEST_DIR.parent / "common"))
import common  # noqa: E402

# Google のテストは、gcc 13 までにない std::sinf や __has_feature を使うので、Linux は gcc 14 で作る
LINUX_ARGS = ["-DCMAKE_C_COMPILER=gcc-14", "-DCMAKE_CXX_COMPILER=g++-14"]


# 作ったテストを emsdk の node で走らせる CMake の引数を返す
#   -DCMAKE_CROSSCOMPILING_EMULATOR=<EMSDK>/node/<版>/bin/node
def wasm_test_args():
    node = sorted(Path(os.environ["EMSDK"]).glob("node/*/bin/node*"))[-1]
    return [f"-DCMAKE_CROSSCOMPILING_EMULATOR={node.as_posix()}"]


def main():
    target = common.parse_target("使い方：python run.py test [wasm]")
    platform, env, args = common.prepare(target)
    if platform == "linux":
        args = [*args, *LINUX_ARGS]
    elif platform == "wasm":
        args = [*args, *wasm_test_args()]
    out = TEST_DIR / "out" / platform
    common.configure_and_build(TEST_DIR, out, env, args)
    # 作ったテストを走らせる
    #   ctest --test-dir test/out/<環境> --output-on-failure
    common.run(["ctest", "--test-dir", out, "--output-on-failure"], env)


if __name__ == "__main__":
    main()
