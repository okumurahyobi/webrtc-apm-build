# WebRTC APM Build

最終更新日: 2026-11-18

WebRTC の音声処理モジュール（Audio Processing Module、略して APM）をビルドします。

```text
webrtc-apm-build/
├─ src/              ダウンロードした WebRTC のソース（書き換えない）
├─ common/           ビルドとテストで共通の設定と、使うソースの一覧
├─ build/            ビルド
├─ test/             テスト
├─ fetch.py          WebRTC のソースを src/ にダウンロード
├─ run.py            ビルドとテストの入口
└─ versions.ini      ダウンロードするリポジトリとバージョン
```

## バージョン

使用するバージョンは `versions.ini` で管理します。各区切りの `ref` は次のように調べます。

| 区切り | `ref` の調べ方 |
|---|---|
| `[webrtc]` | [Chromium Dash の Branches](https://chromiumdash.appspot.com/branches) で、使う Chromium の版の行の WebRTC の列の番号を、`refs/branch-heads/○○` の ○○ に入れる |

## ソースのダウンロード

Google が公開している WebRTC のソースのうち、APM が使うものを `src/` にダウンロードします。

ダウンロード済みの場合は、`src/` を削除してから実行してください。

```
python fetch.py
```

| ライブラリ | 用途 | ダウンロード元 |
|---|---|---|
| WebRTC | APM のソース | WebRTC |
| abseil-cpp | APM が使う C++ の共通部品 | Chromium |
| pffft | 周波数の計算（FFT） | Chromium |
| rnnoise | 声かどうかの判定（AGC2） | Chromium |
| googletest | Chromium 向けのテストの設定 | Chromium |
| googletest の本体 | テスト | Chromium（googletest の写し） |

## 準備

共通：Python 3

| OS | 要るもの |
|---|---|
| Linux | CMake 3.22 以上、Ninja、gcc 14 |
| Windows | Visual Studio の「C++ によるデスクトップ開発」と「Windows 用 C++ Clang コンパイラ」 |

WebAssembly 版は、上に加えて emsdk 7.0.0 が要ります（下の節）。

## WebAssembly を使う場合

WebAssembly のビルドには Emscripten を使います。[emsdk](https://github.com/emscripten-core/emsdk) を好きな場所にダウンロードし、次のバージョンを入れてください。

Linux：

```
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 7.0.0
./emsdk activate 7.0.0
source ./emsdk_env.sh
```

Windows：

```
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
emsdk.bat install 7.0.0
emsdk.bat activate 7.0.0 --permanent
```

Linux では、端末を開くたびに `source ./emsdk_env.sh` を実行してください。

## ビルド

```
python run.py build
python run.py build wasm
```

`wasm` を付けないときは、実行した OS の版を作ります。出来たものは `build/out/<環境>/` に置かれます。

| 環境 | 出来るもの |
|---|---|
| Linux | `libapm.so` |
| Windows | `apm.dll`、`apm.lib` |
| WebAssembly | `apm.js`、`apm.wasm` |

## 高速化（WebAssembly、任意）

WebAssembly 版では、pffft（周波数の計算）が SIMD を使わない版になります。pffft は、x86 か ARM のときだけ SIMD の版を使うためです。

SIMD の版を使うには、`src/third_party/pffft/src/pffft.c` の `SSE1 support macros` のすぐ下にある `#elif` の条件の最後に、次を足してからビルドします。SSE の命令は、Emscripten が WebAssembly の SIMD に置き換えます。

```
|| (defined(__wasm_simd128__) && defined(__SSE__))
```

この節を書いたときは、WebRTC は M158（`refs/branch-heads/8127`）で、この `#elif` は 134 行目にありました。

ダウンロードし直したときは、もう一度足してください。

## 使い方

ブリッジのファイルは `build/bridge/` にあります。次のような使い方を想定しています。

1. `apm_create` で作る
2. `apm_apply_config` で設定を渡す
3. 処理したい音（録った音）を、10ms ずつ `apm_process_stream` に入れる
4. `apm_destroy` で消す

このほか、次の関数も使えます。

- `apm_analyze_reverse_stream`：エコー除去に使う、鳴らしている音を入れる。`apm_process_stream` の手前で、同じ 10ms 分ずつ入れる
- `apm_set_stream_delay_ms`：鳴らしている音と録った音のずれの見当を伝える
- `apm_get_stats`：統計を読む

WebAssembly 版の例です（48kHz、1ch）。

```js
import createApmModule from "./apm.js";

const apm = await createApmModule();
const handle = apm._apm_create(48000, 1);
const ptr = apm._malloc(480 * 2);  // 10ms 分の Int16

// 10ms ごと（render は鳴らしている音、capture は録った音。どちらも 480 個の Int16Array）
apm.HEAP16.set(render, ptr >> 1);
apm._apm_analyze_reverse_stream(handle, ptr);
apm.HEAP16.set(capture, ptr >> 1);
apm._apm_process_stream(handle, ptr);
const output = apm.HEAP16.slice(ptr >> 1, (ptr >> 1) + 480);

apm._free(ptr);
apm._apm_destroy(handle);
```

### 設定

`apm_apply_config` には、次の順に 21 個の数（double）を並べて渡します。オンとオフは 0 か 1 です。範囲の外の値は一番近い値に、NaN と無限大は標準の値に直して入れます。

| 位置 | 名前（`APM_CONFIG_`） | 内容 | 範囲 | 標準 |
|---|---|---|---|---|
| 0 | `HIGH_PASS_ENABLED` | ハイパス（低い音を切る） | 0、1 | 1 |
| 1 | `HIGH_PASS_FULL_BAND` | ハイパスを全帯域にかける | 0、1 | 1 |
| 2 | `ECHO_ENABLED` | エコー除去 | 0、1 | 1 |
| 3 | `ECHO_ENFORCE_HIGH_PASS` | エコー除去のときハイパスを必ずかける | 0、1 | 1 |
| 4 | `NOISE_ENABLED` | ノイズ除去 | 0、1 | 1 |
| 5 | `NOISE_LEVEL` | ノイズ除去の強さ（0=Low、1=Moderate、2=High、3=VeryHigh） | 0〜3 | 2 |
| 6 | `AGC2_ENABLED` | AGC2（声の大きさを自動でそろえる） | 0、1 | 1 |
| 7 | `AGC2_HEADROOM_DB` | AGC2 の余裕（dB） | 0 以上 | 5 |
| 8 | `AGC2_MAX_GAIN_DB` | AGC2 の持ち上げの上限（dB） | 0 より大きい | 50 |
| 9 | `AGC2_INITIAL_GAIN_DB` | AGC2 の最初の持ち上げ（dB） | 0 以上 | 15 |
| 10 | `AGC2_MAX_GAIN_CHANGE_DB_PER_SECOND` | AGC2 の持ち上げを変える速さ（dB/秒） | 0 より大きい | 6 |
| 11 | `AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS` | AGC2 の出力のノイズの上限（dBFS） | -90〜0 | -50 |
| 12 | `AGC2_FIXED_GAIN_DB` | AGC2 の固定の持ち上げ（dB） | 0 以上 50 未満 | 0 |
| 13 | `AGC1_ENABLED` | AGC1 | 0、1 | 0 |
| 14 | `AGC1_MODE` | AGC1 の方式（0=AdaptiveDigital、1=FixedDigital） | 0、1 | 0 |
| 15 | `AGC1_TARGET_LEVEL_DBFS` | AGC1 の目標（3 なら -3 dBFS） | 0〜31 の整数 | 3 |
| 16 | `AGC1_COMPRESSION_GAIN_DB` | AGC1 の上げ幅（dB） | 0〜90 の整数 | 9 |
| 17 | `AGC1_ENABLE_LIMITER` | AGC1 のリミッター | 0、1 | 1 |
| 18 | `LEVEL_ADJUST_ENABLED` | 入力の大きさの調整 | 0、1 | 0 |
| 19 | `LEVEL_ADJUST_PRE_GAIN` | 処理の前の倍率 | 0 以上 | 1.0 |
| 20 | `LEVEL_ADJUST_POST_GAIN` | 処理の後の倍率 | 0 以上 | 1.0 |

戻り値は、そのまま入れたら 0 です。直した項目があれば、その位置のビットを立てた数（位置 7 なら `1 << 7`）を返します。数が 21 個でないときは、何もせず -1 を返します。

## テスト

```
python run.py test
python run.py test wasm
```

Google の APM のテストと、ブリッジのテストを実行します。環境ごとに入れていないテストとその理由は、`test/tests-*.txt` に書いてあります。
