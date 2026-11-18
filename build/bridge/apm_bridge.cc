#include "apm_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/echo_detector_creator.h"
#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"
#include "common_audio/include/audio_util.h"

using webrtc::AudioProcessing;

struct ApmHandle {
	webrtc::scoped_refptr<AudioProcessing> apm;
	webrtc::StreamConfig stream;
	// 写しを float に直して置く作業台（チャンネルごとに 10ms 分）と、各チャンネルの先頭の場所。
	std::vector<float> render_frame;
	std::vector<float*> render_channels;
	// GetStatistics() は、前に呼ばれたあとの最初のフレームの値を返し、取り出されるまで次の値を捨てる。
	// 読んだときに新しい値が返るよう、フレームごとに取り出してここに置く。
	webrtc::AudioProcessingStats latest_stats;
	// APM はエコー除去をオフにしても、エコー除去の統計を最後の値のまま残す。オフのときは窓口で消す。
	bool echo_enabled = false;
	// APM がずれの見当をエコー除去に渡すのは、次の1回の処理のときだけ。どれかの処理をオンやオフにすると、
	// エコー除去ごと作り直されて忘れる。作り直しを気にしなくてよいよう、覚えておき、処理のたびに渡す。
	std::optional<int> stream_delay_ms;
	std::minstd_rand dither_random;
};

namespace {

// apm_create で入れる設定で、NaN と無限大の代わりにも使う。並びは ApmConfigIndex と同じ。
constexpr double kDefaultConfig[APM_CONFIG_COUNT] = {
	1, 1,                       // ハイパス：オン、全帯域
	1, 1,                       // エコー除去：オン、ハイパスを必ずかける
	1, 2,                       // ノイズ除去：オン、High
	1, 5, 50, 15, 6, -50, 0,    // AGC2：オン、headroom、max_gain、initial_gain、変える速さ、出力のノイズの上限、固定の持ち上げ
	0, 0, 3, 9, 1,              // AGC1：オフ、AdaptiveDigital、目標、上げ幅、リミッター
	0, 1.0, 1.0,                // 入力の大きさ：オフ、処理の前の倍率、処理の後の倍率
};

// 戻り値は直した位置（寄せた位置と標準の値にした位置）のビットで、負の数は失敗の印に取っておく。そのため位置は符号のビットより下に収める。
static_assert(APM_CONFIG_COUNT <= 31);

// 「0 より大きい」項目に、0 以下か、APM の中で 0 になるほど小さい値が来たときの寄せ先。
constexpr float kSmallestPositive = 0.1f;
// AGC2 の固定の持ち上げは 50 dB 未満しか受け付けない。50 以上は、50 未満で一番大きい float に寄せる。
const float kAgc2FixedGainDbMax = std::nextafter(50.0f, 0.0f);
// AGC2 の出力のノイズの上限は、Validate は 0 以下かどうかだけを見る。-90 以上は、AGC2 の中の確かめ（RTC_DCHECK）が求める。
constexpr float kAgc2MaxOutputNoiseLevelDbfsMin = -90.0f;

// AGC2 は持ち上げを変える速さを 10ms 分に割って使い、その値が 0 より大きいことを中で確かめる（RTC_DCHECK）。
constexpr float kAgc2GainChangePer10ms = 10.0f / 1000.0f;

// 上限のない項目も、float にしたときに無限大にならない所で止める。
constexpr float kFloatLimit = std::numeric_limits<float>::max();

// 写しに足すノイズの大きさ（最大 ±1e-5、約 -100 dBFS。聞こえない大きさ）。
constexpr float kRenderDitherAmplitude = 1e-5f;

// 届いた数の並びを読む。範囲の外の値は一番近い値に寄せる。
// APM は AGC2 の値が範囲の外だと AGC2 を黙って切るため、APM に渡す前にここで収める。
// 範囲の端は float で表せる値にしてあり、寄せた値は float にしても範囲に入る。
// NaN と無限大は近い値が決まらないので、標準の値にする。寄せた位置と標準の値にした位置は、adjusted_mask() で分かる。
class ConfigReader {
public:
	explicit ConfigReader(const double* values) : values_(values) {}

	bool On(ApmConfigIndex i) { return IntBetween(i, 0, 1) == 1; }
	float AtLeast(ApmConfigIndex i, float min) { return Between(i, min, kFloatLimit); }

	float Between(ApmConfigIndex i, float min, float max) {
		const double value = Value(i);
		const double clamped = std::clamp(value, static_cast<double>(min), static_cast<double>(max));
		if (clamped != value) Mark(i);
		return static_cast<float>(clamped);
	}

	// 整数の項目は、小数も「範囲の外」とみなして一番近い整数に寄せる。
	int IntBetween(ApmConfigIndex i, int min, int max) {
		const double value = Value(i);
		const double clamped = std::clamp(std::round(value), static_cast<double>(min), static_cast<double>(max));
		if (clamped != value) Mark(i);
		return static_cast<int>(clamped);
	}

	// 「0 より大きい」項目。APM が使うときに scale 倍しても 0 より大きい値だけを受け付ける。
	// float にして 0 になるごく小さな値も、0 以下とみなす。
	float Positive(ApmConfigIndex i, float scale = 1.0f) {
		const float value = AtLeast(i, 0);
		if (value * scale > 0) return value;
		Mark(i);
		return kSmallestPositive;
	}

	int adjusted_mask() const { return adjusted_mask_; }

private:
	double Value(ApmConfigIndex i) {
		const double value = values_[i];
		if (std::isfinite(value)) return value;
		Mark(i);
		return kDefaultConfig[i];
	}

	void Mark(ApmConfigIndex i) { adjusted_mask_ |= 1 << i; }

	const double* values_;
	int adjusted_mask_ = 0;
};

AudioProcessing::Config ToApmConfig(ConfigReader& in) {
	AudioProcessing::Config config;
	config.high_pass_filter.enabled = in.On(APM_CONFIG_HIGH_PASS_ENABLED);
	config.high_pass_filter.apply_in_full_band = in.On(APM_CONFIG_HIGH_PASS_FULL_BAND);

	config.echo_canceller.enabled = in.On(APM_CONFIG_ECHO_ENABLED);
	config.echo_canceller.enforce_high_pass_filtering = in.On(APM_CONFIG_ECHO_ENFORCE_HIGH_PASS);

	config.noise_suppression.enabled = in.On(APM_CONFIG_NOISE_ENABLED);
	config.noise_suppression.level = static_cast<AudioProcessing::Config::NoiseSuppression::Level>(
		in.IntBetween(APM_CONFIG_NOISE_LEVEL, 0, 3));

	auto& agc2 = config.gain_controller2;
	agc2.enabled = in.On(APM_CONFIG_AGC2_ENABLED);
	agc2.adaptive_digital.enabled = agc2.enabled;
	agc2.adaptive_digital.headroom_db = in.AtLeast(APM_CONFIG_AGC2_HEADROOM_DB, 0);
	agc2.adaptive_digital.max_gain_db = in.Positive(APM_CONFIG_AGC2_MAX_GAIN_DB);
	agc2.adaptive_digital.initial_gain_db = in.AtLeast(APM_CONFIG_AGC2_INITIAL_GAIN_DB, 0);
	agc2.adaptive_digital.max_gain_change_db_per_second =
		in.Positive(APM_CONFIG_AGC2_MAX_GAIN_CHANGE_DB_PER_SECOND, kAgc2GainChangePer10ms);
	agc2.adaptive_digital.max_output_noise_level_dbfs =
		in.Between(APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, kAgc2MaxOutputNoiseLevelDbfsMin, 0);
	agc2.fixed_digital.gain_db = in.Between(APM_CONFIG_AGC2_FIXED_GAIN_DB, 0, kAgc2FixedGainDbMax);

	auto& agc1 = config.gain_controller1;
	agc1.enabled = in.On(APM_CONFIG_AGC1_ENABLED);
	agc1.mode = in.On(APM_CONFIG_AGC1_MODE)
		? AudioProcessing::Config::GainController1::kFixedDigital
		: AudioProcessing::Config::GainController1::kAdaptiveDigital;
	agc1.target_level_dbfs = in.IntBetween(APM_CONFIG_AGC1_TARGET_LEVEL_DBFS, 0, 31);
	agc1.compression_gain_db = in.IntBetween(APM_CONFIG_AGC1_COMPRESSION_GAIN_DB, 0, 90);
	agc1.enable_limiter = in.On(APM_CONFIG_AGC1_ENABLE_LIMITER);
	// OS のマイクの音量を動かす部分。ブリッジはマイクの音量をやり取りする関数を出さないので切る。
	// APM の標準はオンで、オンのままだと目標や上げ幅などの設定が AGC1 に渡らない。
	agc1.analog_gain_controller.enabled = false;

	config.capture_level_adjustment.enabled = in.On(APM_CONFIG_LEVEL_ADJUST_ENABLED);
	config.capture_level_adjustment.pre_gain_factor = in.AtLeast(APM_CONFIG_LEVEL_ADJUST_PRE_GAIN, 0);
	config.capture_level_adjustment.post_gain_factor = in.AtLeast(APM_CONFIG_LEVEL_ADJUST_POST_GAIN, 0);
	return config;
}

double OrNaN(std::optional<double> value) {
	return value ? *value : std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

extern "C" {

ApmHandle* apm_create(int sample_rate, int num_channels) {
	if (sample_rate != 8000 && sample_rate != 16000 && sample_rate != 32000 && sample_rate != 48000) {
		return nullptr;
	}
	if (num_channels < 1) return nullptr;
	auto* handle = new ApmHandle;
	handle->apm = webrtc::BuiltinAudioProcessingBuilder()
		.SetEchoDetector(webrtc::CreateEchoDetector())
		.Build(webrtc::CreateEnvironment());
	handle->stream = webrtc::StreamConfig(sample_rate, num_channels);
	const size_t frames = handle->stream.num_frames();
	handle->render_frame.resize(frames * num_channels);
	for (int ch = 0; ch < num_channels; ++ch) {
		handle->render_channels.push_back(handle->render_frame.data() + ch * frames);
	}
	apm_apply_config(handle, kDefaultConfig, APM_CONFIG_COUNT);
	return handle;
}

int apm_apply_config(ApmHandle* handle, const double* values, int count) {
	if (count != APM_CONFIG_COUNT) return -1;
	auto reader = ConfigReader(values);
	const AudioProcessing::Config config = ToApmConfig(reader);
	handle->apm->ApplyConfig(config);
	handle->echo_enabled = config.echo_canceller.enabled;
	return reader.adjusted_mask();
}

int apm_process_stream(ApmHandle* handle, int16_t* samples) {
	if (handle->stream_delay_ms) handle->apm->set_stream_delay_ms(*handle->stream_delay_ms);

	const int result = handle->apm->ProcessStream(samples, handle->stream, handle->stream, samples);

	handle->latest_stats = handle->apm->GetStatistics();
	if (!handle->echo_enabled) {
		handle->latest_stats.echo_return_loss.reset();
		handle->latest_stats.echo_return_loss_enhancement.reset();
		handle->latest_stats.delay_ms.reset();
	}
	return result;
}

int apm_analyze_reverse_stream(ApmHandle* handle, const int16_t* samples) {
	const size_t frames = handle->stream.num_frames();
	const size_t channels = handle->stream.num_channels();
	// 写しがちょうど 0 になると、エコー除去の中の計算がごく小さな数（非正規化数）になり、とても遅くなる。
	// wasm と gcc で作った版では APM がこの数を 0 に切り捨てられないため、聞こえない大きさのノイズを足して 0 にしない。どの環境でも同じ処理になるよう、切り捨てられる版でも足す。
	// ノイズは float でしか足せないので、交互の並びを、チャンネルごとの float に分けながら足す。
	std::uniform_real_distribution<float> dither(-kRenderDitherAmplitude, kRenderDitherAmplitude);
	for (size_t i = 0; i < frames; ++i) {
		for (size_t ch = 0; ch < channels; ++ch) {
			const float sample = webrtc::S16ToFloat(samples[i * channels + ch]);
			handle->render_channels[ch][i] = sample + dither(handle->dither_random);
		}
	}
	return handle->apm->AnalyzeReverseStream(handle->render_channels.data(), handle->stream);
}

int apm_set_stream_delay_ms(ApmHandle* handle, int delay_ms) {
	handle->stream_delay_ms = delay_ms;
	return handle->apm->set_stream_delay_ms(delay_ms);
}

void apm_get_stats(const ApmHandle* handle, double* out) {
	const webrtc::AudioProcessingStats& stats = handle->latest_stats;
	out[APM_STATS_ECHO_RETURN_LOSS] = OrNaN(stats.echo_return_loss);
	out[APM_STATS_ECHO_RETURN_LOSS_ENHANCEMENT] = OrNaN(stats.echo_return_loss_enhancement);
	out[APM_STATS_DELAY_MS] = OrNaN(stats.delay_ms);
	out[APM_STATS_RESIDUAL_ECHO_LIKELIHOOD] = OrNaN(stats.residual_echo_likelihood);
	out[APM_STATS_RESIDUAL_ECHO_LIKELIHOOD_RECENT_MAX] = OrNaN(stats.residual_echo_likelihood_recent_max);
}

void apm_destroy(ApmHandle* handle) { delete handle; }

}  // extern "C"
