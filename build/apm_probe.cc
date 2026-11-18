// ブリッジが使う APM の機能を呼ぶ小さなプログラム。リンクのエラーを辿って、ビルドに要るファイルを決めるために使う。
//
// 略語
// - APM（Audio Processing Module）：WebRTC の音声処理。下の機能をまとめたもの
// - AEC3（Acoustic Echo Canceller 3）：エコー除去。スピーカーから出た音がマイクに戻ってきた分を消す。3 は世代
// - NS（Noise Suppression）：ノイズ抑制。空調や雑音など、声でない音を小さくする
// - AGC1、AGC2（Automatic Gain Control）：音量の自動調整。声の大きさを聞きやすい大きさに揃える。1 が古い世代、2 が新しい世代
// - HPF（High Pass Filter）：低い音（風や振動）を切る
//
// 使っているもの
// - Builder：BuiltinAudioProcessingBuilder、SetEchoDetector（エコーが残っている見込みを統計に出す）
// - 設定：pipeline（標準の値）、high_pass_filter、echo_canceller（AEC3）、noise_suppression（kHigh）、
//   gain_controller1（デジタルだけ）、gain_controller2（adaptive_digital と fixed_digital）、
//   capture_level_adjustment（処理の前後の音量の倍率）。細かい値は標準の値でも書いている
// - 関数：ApplyConfig、ProcessReverseStream と ProcessStream（Int16 版）、set_stream_delay_ms、GetStatistics
//   ブリッジが鳴らしている音に使う float 版の AnalyzeReverseStream も同じファイルにあるので、要るファイルは同じ
// AGC1 と AGC2 は、どちらに要るファイルも揃うように両方オンにしている。
//
// 使っていないもの（ブリッジが出していない機能）
// - pre_amplifier：決まった倍率で音量を上げる古い方式。capture_level_adjustment で代わりになる
// - capture_level_adjustment の analog_mic_gain_emulation：アナログのマイク音量のまね
// - echo_canceller の export_linear_aec_output、GetLinearAecOutput：エコー除去の途中の音を受け取る
// - noise_suppression の analyze_linear_aec_output_when_available：ノイズの見積もりにエコー除去の途中の音を使う
// - transient_suppression、set_stream_key_pressed：打鍵音などを抑える。キーが押されたことは呼ぶ側が知らせる
// - AGC1 の analog_gain_controller と clipping_predictor、AGC2 の input_volume_controller、
//   set_stream_analog_level、recommended_stream_analog_level：マイクの入力音量を動かす
// - SetEchoCancellerConfig、SetEchoControlFactory：AEC3 の細かい設定、別のエコー除去への差し替え
// - SetCapturePostProcessing、SetRenderPreProcessing、SetCaptureAnalyzer：自分の処理や解析を途中に挟む
// - SetNeuralResidualEchoEstimator：ニューラルネットで残ったエコーを見積もる
// - float 版の ProcessStream と ProcessReverseStream：Int16 版と同じファイル（audio_processing_impl.cc）にあり、呼ばなくても要るファイルは変わらない
// - SetRuntimeSetting：動作中に音量の倍率などを変える
// - CreateAndAttachAecDump、AttachAecDump、DetachAecDump：音と設定をファイルに記録する（AecDump）
#include <cstdint>
#include <cstdio>
#include <vector>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/echo_detector_creator.h"
#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"

namespace {

using webrtc::AudioProcessing;

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kFrameSamples = kSampleRate / 100 * kChannels;
constexpr int kFrames = 100;

AudioProcessing::Config MakeConfig() {
	AudioProcessing::Config config;
	config.high_pass_filter.enabled = true;
	config.high_pass_filter.apply_in_full_band = true;
	config.echo_canceller.enabled = true;
	config.echo_canceller.enforce_high_pass_filtering = true;
	config.noise_suppression.enabled = true;
	config.noise_suppression.level = AudioProcessing::Config::NoiseSuppression::kHigh;
	config.gain_controller1.enabled = true;
	config.gain_controller1.mode = AudioProcessing::Config::GainController1::kAdaptiveDigital;
	config.gain_controller1.target_level_dbfs = 3;
	config.gain_controller1.compression_gain_db = 9;
	config.gain_controller1.enable_limiter = true;
	config.gain_controller1.analog_gain_controller.enabled = false;
	config.gain_controller2.enabled = true;
	config.gain_controller2.adaptive_digital.enabled = true;
	config.gain_controller2.adaptive_digital.headroom_db = 5.0f;
	config.gain_controller2.adaptive_digital.max_gain_db = 50.0f;
	config.gain_controller2.adaptive_digital.initial_gain_db = 15.0f;
	config.gain_controller2.adaptive_digital.max_gain_change_db_per_second = 6.0f;
	config.gain_controller2.adaptive_digital.max_output_noise_level_dbfs = -50.0f;
	config.gain_controller2.fixed_digital.gain_db = 0.0f;
	config.capture_level_adjustment.enabled = true;
	config.capture_level_adjustment.pre_gain_factor = 1.0f;
	config.capture_level_adjustment.post_gain_factor = 1.0f;
	return config;
}

}  // namespace

int main() {
	webrtc::scoped_refptr<AudioProcessing> apm = webrtc::BuiltinAudioProcessingBuilder()
		.SetEchoDetector(webrtc::CreateEchoDetector())
		.Build(webrtc::CreateEnvironment());
	if (!apm) {
		std::fprintf(stderr, "APM を作れませんでした\n");
		return 1;
	}
	apm->ApplyConfig(MakeConfig());

	const webrtc::StreamConfig stream(kSampleRate, kChannels);
	std::vector<int16_t> capture(kFrameSamples);
	std::vector<int16_t> render(kFrameSamples);
	for (int i = 0; i < kFrames; ++i) {
		const int reverse_result = apm->ProcessReverseStream(render.data(), stream, stream, render.data());
		if (reverse_result != AudioProcessing::kNoError) {
			std::fprintf(stderr, "ProcessReverseStream: %d\n", reverse_result);
			return 1;
		}
		apm->set_stream_delay_ms(50);
		const int capture_result = apm->ProcessStream(capture.data(), stream, stream, capture.data());
		if (capture_result != AudioProcessing::kNoError) {
			std::fprintf(stderr, "ProcessStream: %d\n", capture_result);
			return 1;
		}
	}

	const webrtc::AudioProcessingStats stats = apm->GetStatistics();
	std::printf("echo_return_loss: %f\n", stats.echo_return_loss.value_or(-1));
	std::printf("delay_ms: %d\n", stats.delay_ms.value_or(-1));
	std::printf("residual_echo_likelihood: %f\n", stats.residual_echo_likelihood.value_or(-1));
	return 0;
}
