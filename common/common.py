"""build/build.py と test/test.py が使う、CMake を走らせるまでの準備と、走らせる関数。

Windows は Visual Studio の開発者用の環境と clang-cl、wasm は emsdk の toolchain を用意する。
"""
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
VS_CLANG_COMPONENT = "Microsoft.VisualStudio.Component.VC.Llvm.Clang"
VS_CMAKE_COMPONENT = "Microsoft.VisualStudio.Component.VC.CMake.Project"


def run(args, env=None):
    args = [str(arg) for arg in args]
    # Windows は、渡した環境変数ではなく自分の PATH からコマンドを探すので、渡す PATH で探しておく
    if env:
        args[0] = shutil.which(args[0], path=env["PATH"]) or args[0]
    subprocess.run(args, check=True, env=env)


# run.py の後ろの言葉（wasm か、なし）を返す。ほかの言葉なら使い方を出して止める
def parse_target(usage):
    target = sys.argv[1] if len(sys.argv) > 1 else None
    if target not in (None, "wasm"):
        sys.exit(usage)
    return target


# clang-cl と CMake を Visual Studio から借りるため、その2つが入った Visual Studio の場所を返す。
# 場所は、Visual Studio に付いている vswhere.exe に次のように聞いて調べる
#   vswhere.exe -latest -products * -requires <Clang の部品> <CMake の部品> -property installationPath
def find_visual_studio():
    result = subprocess.run(
        [VSWHERE, "-latest", "-products", "*", "-requires", VS_CLANG_COMPONENT, VS_CMAKE_COMPONENT, "-property", "installationPath"],
        capture_output=True, text=True, check=True)
    return Path(result.stdout.strip())


# cmake、ninja、リンカーは、Visual Studio の開発者用の環境（VsDevCmd.bat）で使えるようになる。
# その環境変数を読み取り、このあとのコマンドに渡す。
def visual_studio_env(vs_dir):
    vsdevcmd = vs_dir / "Common7" / "Tools" / "VsDevCmd.bat"
    # 引数を1つの文字列で渡すのは、cmd の引用符の扱いを Python に書き換えさせないため
    result = subprocess.run(
        f'cmd /d /s /c ""{vsdevcmd}" -arch=amd64 -host_arch=amd64 -no_logo && set"',
        capture_output=True, text=True, check=True)
    env = {}
    for line in result.stdout.splitlines():
        name, sep, value = line.partition("=")
        if sep:
            # Windows の環境変数の名前は大文字と小文字を区別しない。Path と PATH を同じものとして引けるよう、大文字にそろえる
            env[name.upper()] = value
    return env


# clang-cl で作らせる CMake の引数を返す
#   -DCMAKE_C_COMPILER=<VS>/VC/Tools/Llvm/x64/bin/clang-cl.exe -DCMAKE_CXX_COMPILER=<VS>/VC/Tools/Llvm/x64/bin/clang-cl.exe
def windows_args(vs_dir):
    clang_cl = (vs_dir / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-cl.exe").as_posix()
    return [f"-DCMAKE_C_COMPILER={clang_cl}", f"-DCMAKE_CXX_COMPILER={clang_cl}"]


# Emscripten で wasm 向けに作らせる CMake の引数を返す
#   -DCMAKE_TOOLCHAIN_FILE=<EMSDK>/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
def wasm_args():
    toolchain = Path(os.environ["EMSDK"]) / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake"
    return [f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}"]


def prepare(target):
    """target は None（走らせた OS の版）か "wasm"。環境の名前、コマンドに渡す環境変数、CMake の引数を返す。"""
    system = platform.system()
    env = None
    native_args = []
    if system == "Windows":
        vs_dir = find_visual_studio()
        env = visual_studio_env(vs_dir)
        native_args = windows_args(vs_dir)
    platform_name = target or system.lower()
    args = wasm_args() if target == "wasm" else native_args
    return platform_name, env, args


# 次の2つ（設定とビルド）を実行する
#   cmake -S <build か test> -B <build か test>/out/<環境> -G Ninja -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=<この python> <環境ごとの引数>
#   cmake --build <build か test>/out/<環境>
# <環境ごとの引数> は、Linux はなし、Windows は windows_args、wasm は wasm_args。テストのときは test.py が足したものも付く
def configure_and_build(source, out, env, args):
    run(["cmake", "-S", source, "-B", out, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         f"-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}", *args], env)
    run(["cmake", "--build", out], env)
