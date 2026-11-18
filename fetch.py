#!/usr/bin/env python3
"""WebRTC 本体と、APM とテストが使う third_party を src/ にダウンロードする。WebRTC の版は versions.ini。"""
import base64
import configparser
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

WEBRTC_URL = "https://webrtc.googlesource.com/src"
THIRD_PARTY_URL = "https://chromium.googlesource.com/chromium/src/third_party"
GOOGLETEST_URL = "https://chromium.googlesource.com/external/github.com/google/googletest.git"

THIRD_PARTY_DIRS = ["abseil-cpp", "pffft", "rnnoise", "googletest"]

ROOT = Path(__file__).resolve().parent


def load_versions():
    versions = configparser.ConfigParser()
    with open(ROOT / "versions.ini", encoding="utf-8") as f:
        versions.read_file(f)
    return versions


# WebRTC の DEPS には、ライブラリごとに使うコミットが指定されている。
# '<名前>': の次の '<URL>@<コミット>' から、コミットのハッシュを取る。
def get_deps_ref(webrtc_ref, name):
    url = f"{WEBRTC_URL}/+/{webrtc_ref}/DEPS?format=TEXT"
    with urllib.request.urlopen(url) as response:
        deps = base64.b64decode(response.read()).decode("utf-8")
    match = re.search(rf"'{re.escape(name)}':\s*'[^']*@([0-9a-f]{{40}})'", deps)
    if not match:
        sys.exit(f"DEPS に '{name}' が見つかりません。")
    return match.group(1)


def git(repo, *args):
    subprocess.run(["git", "-C", str(repo), *args], check=True)


def init_repo(repo, url):
    repo.mkdir(parents=True, exist_ok=True)
    git(repo, "init", "-q")
    # Windows で取っても、Google のソースと同じ LF のまま置く。
    git(repo, "config", "core.autocrlf", "false")
    git(repo, "remote", "add", "origin", url)


def fetch_commit(repo, url, ref):
    init_repo(repo, url)
    git(repo, "fetch", "--depth", "1", "origin", ref)
    git(repo, "checkout", "-q", "FETCH_HEAD")


def fetch_third_party(repo, ref):
    # Chromium の third_party はとても大きいので、使うフォルダだけを取る。
    init_repo(repo, THIRD_PARTY_URL)
    git(repo, "sparse-checkout", "init", "--cone")
    git(repo, "sparse-checkout", "set", *THIRD_PARTY_DIRS)
    git(repo, "fetch", "--depth", "1", "--filter=blob:none", "origin", ref)
    git(repo, "checkout", "-q", "FETCH_HEAD")


def main():
    versions = load_versions()
    src = ROOT / "src"
    if src.exists():
        sys.exit(f"{src} がすでにあります。取り直すときは先に消してください。")

    webrtc_ref = versions["webrtc"]["ref"]
    third_party_ref = get_deps_ref(webrtc_ref, "src/third_party")
    googletest_ref = get_deps_ref(webrtc_ref, "src/third_party/googletest/src")

    fetch_commit(src, WEBRTC_URL, webrtc_ref)
    fetch_third_party(src / "third_party", third_party_ref)
    # googletest の中身は third_party とは別のリポジトリ。
    fetch_commit(src / "third_party" / "googletest" / "src", GOOGLETEST_URL, googletest_ref)

    print(f"{src} に取り終わりました")


if __name__ == "__main__":
    main()
