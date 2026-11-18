// APM を外から使うための C の窓口。1つのハンドルが APM を1つ持ち、作るときのチャンネルの数のまま音を通す。
// 音はチャンネルを交互に並べたもの。1回に 10ms 分（sample_rate / 100 × チャンネルの数）ちょうどを渡す。
// 音を渡す関数の名前は、中で呼ぶ APM の関数にそろえている。stream は録った音（capture）、reverse stream は鳴らしている音（render）。
// handle と配列に NULL は渡さない。1つのハンドルの関数を、複数のスレッドから同時に呼ばない（ハンドルの中の作業台や覚えている値を、ロックなしで読み書きするため）。
#pragma once

#include <stdint.h>

// 窓口の関数のうち、外から呼べるのはこの印を付けたものだけ。Linux の共有ライブラリはほかの名前を隠して作るので、ここで見えるようにする。
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define APM_BRIDGE_EXPORT EMSCRIPTEN_KEEPALIVE
#elif defined(_WIN32)
#define APM_BRIDGE_EXPORT __declspec(dllexport)
#else
#define APM_BRIDGE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ApmHandle ApmHandle;

// apm_apply_config に渡す数の並びの位置。呼ぶ側も同じ順に並べる。
// オンとオフは 0 か 1。項目を足すときは最後に足す（前の位置を変えない）。
enum ApmConfigIndex {
	APM_CONFIG_HIGH_PASS_ENABLED,
	APM_CONFIG_HIGH_PASS_FULL_BAND,
	APM_CONFIG_ECHO_ENABLED,
	APM_CONFIG_ECHO_ENFORCE_HIGH_PASS,
	APM_CONFIG_NOISE_ENABLED,
	APM_CONFIG_NOISE_LEVEL,  // 0=Low、1=Moderate、2=High、3=VeryHigh
	APM_CONFIG_AGC2_ENABLED,
	APM_CONFIG_AGC2_HEADROOM_DB,
	APM_CONFIG_AGC2_MAX_GAIN_DB,
	APM_CONFIG_AGC2_INITIAL_GAIN_DB,
	APM_CONFIG_AGC2_MAX_GAIN_CHANGE_DB_PER_SECOND,
	APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS,
	APM_CONFIG_AGC2_FIXED_GAIN_DB,
	APM_CONFIG_AGC1_ENABLED,
	APM_CONFIG_AGC1_MODE,  // 0=AdaptiveDigital、1=FixedDigital
	APM_CONFIG_AGC1_TARGET_LEVEL_DBFS,
	APM_CONFIG_AGC1_COMPRESSION_GAIN_DB,
	APM_CONFIG_AGC1_ENABLE_LIMITER,
	APM_CONFIG_LEVEL_ADJUST_ENABLED,
	APM_CONFIG_LEVEL_ADJUST_PRE_GAIN,
	APM_CONFIG_LEVEL_ADJUST_POST_GAIN,
	APM_CONFIG_COUNT
};

// apm_get_stats が out に入れる順。値がないもの（エコー除去がオフのときの ERL、ERLE、DELAY_MS など）は NaN。
enum ApmStatsIndex {
	APM_STATS_ECHO_RETURN_LOSS,
	APM_STATS_ECHO_RETURN_LOSS_ENHANCEMENT,
	APM_STATS_DELAY_MS,
	APM_STATS_RESIDUAL_ECHO_LIKELIHOOD,
	APM_STATS_RESIDUAL_ECHO_LIKELIHOOD_RECENT_MAX,
	APM_STATS_COUNT
};

// sample_rate は 8000、16000、32000、48000 のどれか。num_channels は 1 以上。それ以外のときは NULL を返す。ブリッジの標準の設定（apm_bridge.cc の kDefaultConfig）で作る。
APM_BRIDGE_EXPORT ApmHandle* apm_create(int sample_rate, int num_channels);

// values は APM_CONFIG_COUNT 個の double。範囲の外の値は一番近い値に、NaN と無限大は標準の値にして入れる。
// 「0 より大きい」項目に、0 以下か、APM の中で 0 になるほど小さい値が来たときは 0.1 にする。
// 整数の項目（オンとオフも）に小数が来たときは、一番近い整数にする。
// 戻り値は、そのまま入れたら 0、直した項目があればその位置のビットを立てた数（位置 7 なら 1 << 7）。
// count が APM_CONFIG_COUNT と違うとき（呼ぶ側と版がずれているとき）は、何もせず -1。
APM_BRIDGE_EXPORT int apm_apply_config(ApmHandle* handle, const double* values, int count);

// 録った音 10ms 分を処理して、同じ場所に書き戻す。戻り値は APM のエラーコード（0 が成功）。
APM_BRIDGE_EXPORT int apm_process_stream(ApmHandle* handle, int16_t* samples);

// 鳴らしている音の写し 10ms 分。エコー除去とエコーの検出器が手本に使うだけで、samples は書き換えない。
// どちらかを使う間は、鳴らす音がないときも 0 を渡し続ける。戻り値は APM のエラーコード（0 が成功）。
APM_BRIDGE_EXPORT int apm_analyze_reverse_stream(ApmHandle* handle, const int16_t* samples);

// 鳴らしている音と録った音のずれの見当（ms）。APM はこれを出発点にして、自分でも見積もる。呼ばなくてもよい。
// 一度呼ぶと、窓口が覚えておき、apm_process_stream のたびに渡す（何も渡さない状態に戻すには作り直す）。
// 0〜500 の外は APM が端の値に直して入れ、警告の -13 を返す。
APM_BRIDGE_EXPORT int apm_set_stream_delay_ms(ApmHandle* handle, int delay_ms);

// out は APM_STATS_COUNT 個の double。直前の apm_process_stream のときの値を入れる。
APM_BRIDGE_EXPORT void apm_get_stats(const ApmHandle* handle, double* out);

APM_BRIDGE_EXPORT void apm_destroy(ApmHandle* handle);

#ifdef __cplusplus
}  // extern "C"
#endif
