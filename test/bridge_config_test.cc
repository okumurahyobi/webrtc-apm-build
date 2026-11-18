// C の窓口を確かめる。
// 1. apm_apply_config で渡した値が、APM の設定の正しい項目に入るか。
//    apm_bridge.cc の対応（位置 → 項目）とは別に、ここにも対応表を書き、2つが一致するかを見る。
// 2. 範囲の外の値が一番近い値に、NaN と無限大が標準の値に直され、直した位置が戻り値に出て、AGC2 が切れないか。数が違うときは -1 で、何も入らないか。
// 3. 音を流して、処理と統計が動くか。処理を全部オフにすると音がそのまま返るか。
// 4. ずれの見当の戻り値と、対応していないレートやチャンネルの数 0 で作れないこと。
// ApmHandle の中の APM から GetConfig() で読み出すため、apm_bridge.cc をそのまま取り込む。
#include "../build/bridge/apm_bridge.cc"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numbers>
#include <vector>

namespace {

using Config = webrtc::AudioProcessing::Config;

struct Field {
	ApmConfigIndex index;
	const char* name;
	double test_value;  // 標準と違い、float でもぴったり表せる値
	std::function<double(const Config&)> read;
};

const std::vector<Field> kFields = {
	{APM_CONFIG_HIGH_PASS_ENABLED, "high_pass_filter.enabled", 0,
		[](const Config& c) { return c.high_pass_filter.enabled; }},
	{APM_CONFIG_HIGH_PASS_FULL_BAND, "high_pass_filter.apply_in_full_band", 0,
		[](const Config& c) { return c.high_pass_filter.apply_in_full_band; }},
	{APM_CONFIG_ECHO_ENABLED, "echo_canceller.enabled", 0,
		[](const Config& c) { return c.echo_canceller.enabled; }},
	{APM_CONFIG_ECHO_ENFORCE_HIGH_PASS, "echo_canceller.enforce_high_pass_filtering", 0,
		[](const Config& c) { return c.echo_canceller.enforce_high_pass_filtering; }},
	{APM_CONFIG_NOISE_ENABLED, "noise_suppression.enabled", 0,
		[](const Config& c) { return c.noise_suppression.enabled; }},
	{APM_CONFIG_NOISE_LEVEL, "noise_suppression.level", 0,
		[](const Config& c) { return static_cast<int>(c.noise_suppression.level); }},
	{APM_CONFIG_AGC2_ENABLED, "gain_controller2.enabled", 0,
		[](const Config& c) { return c.gain_controller2.enabled; }},
	{APM_CONFIG_AGC2_HEADROOM_DB, "gain_controller2.adaptive_digital.headroom_db", 7.5,
		[](const Config& c) { return c.gain_controller2.adaptive_digital.headroom_db; }},
	{APM_CONFIG_AGC2_MAX_GAIN_DB, "gain_controller2.adaptive_digital.max_gain_db", 33.5,
		[](const Config& c) { return c.gain_controller2.adaptive_digital.max_gain_db; }},
	{APM_CONFIG_AGC2_INITIAL_GAIN_DB, "gain_controller2.adaptive_digital.initial_gain_db", 9.25,
		[](const Config& c) { return c.gain_controller2.adaptive_digital.initial_gain_db; }},
	{APM_CONFIG_AGC2_MAX_GAIN_CHANGE_DB_PER_SECOND, "gain_controller2.adaptive_digital.max_gain_change_db_per_second", 3.5,
		[](const Config& c) { return c.gain_controller2.adaptive_digital.max_gain_change_db_per_second; }},
	{APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, "gain_controller2.adaptive_digital.max_output_noise_level_dbfs", -61.5,
		[](const Config& c) { return c.gain_controller2.adaptive_digital.max_output_noise_level_dbfs; }},
	{APM_CONFIG_AGC2_FIXED_GAIN_DB, "gain_controller2.fixed_digital.gain_db", 12.5,
		[](const Config& c) { return c.gain_controller2.fixed_digital.gain_db; }},
	{APM_CONFIG_AGC1_ENABLED, "gain_controller1.enabled", 1,
		[](const Config& c) { return c.gain_controller1.enabled; }},
	{APM_CONFIG_AGC1_MODE, "gain_controller1.mode", 1,
		[](const Config& c) {
			if (c.gain_controller1.mode == Config::GainController1::kAdaptiveDigital) return 0;
			if (c.gain_controller1.mode == Config::GainController1::kFixedDigital) return 1;
			return -1;
		}},
	{APM_CONFIG_AGC1_TARGET_LEVEL_DBFS, "gain_controller1.target_level_dbfs", 17,
		[](const Config& c) { return c.gain_controller1.target_level_dbfs; }},
	{APM_CONFIG_AGC1_COMPRESSION_GAIN_DB, "gain_controller1.compression_gain_db", 44,
		[](const Config& c) { return c.gain_controller1.compression_gain_db; }},
	{APM_CONFIG_AGC1_ENABLE_LIMITER, "gain_controller1.enable_limiter", 0,
		[](const Config& c) { return c.gain_controller1.enable_limiter; }},
	{APM_CONFIG_LEVEL_ADJUST_ENABLED, "capture_level_adjustment.enabled", 1,
		[](const Config& c) { return c.capture_level_adjustment.enabled; }},
	{APM_CONFIG_LEVEL_ADJUST_PRE_GAIN, "capture_level_adjustment.pre_gain_factor", 1.75,
		[](const Config& c) { return c.capture_level_adjustment.pre_gain_factor; }},
	{APM_CONFIG_LEVEL_ADJUST_POST_GAIN, "capture_level_adjustment.post_gain_factor", 0.5,
		[](const Config& c) { return c.capture_level_adjustment.post_gain_factor; }},
};

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// 入れる値、入るはずの値、寄せた印が立つか。
struct ClampCase {
	ApmConfigIndex index;
	double input;
	double expected;
	bool adjusted;
};

// オンとオフはどれも同じ読み方なので、代表1つ（ハイパス）で確かめる。
// 数値と整数の項目は項目ごとに範囲を書いているので、全部の項目を1件以上確かめる。
const std::vector<ClampCase> kClampCases = {
	// オンとオフ（0 か 1）
	{APM_CONFIG_HIGH_PASS_ENABLED, -1, 0, true},
	{APM_CONFIG_HIGH_PASS_ENABLED, 2, 1, true},
	{APM_CONFIG_HIGH_PASS_ENABLED, 0.4, 0, true},
	{APM_CONFIG_HIGH_PASS_ENABLED, 0.7, 1, true},
	// 整数
	{APM_CONFIG_NOISE_LEVEL, 7, 3, true},
	{APM_CONFIG_NOISE_LEVEL, -2, 0, true},
	{APM_CONFIG_NOISE_LEVEL, 3, 3, false},
	{APM_CONFIG_AGC1_TARGET_LEVEL_DBFS, 40, 31, true},
	{APM_CONFIG_AGC1_TARGET_LEVEL_DBFS, -1, 0, true},
	{APM_CONFIG_AGC1_TARGET_LEVEL_DBFS, 17.5, 18, true},
	{APM_CONFIG_AGC1_TARGET_LEVEL_DBFS, 31, 31, false},
	{APM_CONFIG_AGC1_COMPRESSION_GAIN_DB, 91, 90, true},
	{APM_CONFIG_AGC1_COMPRESSION_GAIN_DB, -5, 0, true},
	// 0 以上
	{APM_CONFIG_AGC2_HEADROOM_DB, -3, 0, true},
	{APM_CONFIG_AGC2_HEADROOM_DB, 0, 0, false},
	{APM_CONFIG_AGC2_HEADROOM_DB, 1e40, kFloatLimit, true},
	{APM_CONFIG_AGC2_INITIAL_GAIN_DB, -1, 0, true},
	{APM_CONFIG_LEVEL_ADJUST_PRE_GAIN, -1, 0, true},
	{APM_CONFIG_LEVEL_ADJUST_POST_GAIN, -0.5, 0, true},
	// 0 より大きい
	{APM_CONFIG_AGC2_MAX_GAIN_DB, 0, kSmallestPositive, true},
	{APM_CONFIG_AGC2_MAX_GAIN_DB, -5, kSmallestPositive, true},
	{APM_CONFIG_AGC2_MAX_GAIN_DB, 1e-50, kSmallestPositive, true},
	{APM_CONFIG_AGC2_MAX_GAIN_DB, 0.05, 0.05, false},
	{APM_CONFIG_AGC2_MAX_GAIN_CHANGE_DB_PER_SECOND, -2, kSmallestPositive, true},
	{APM_CONFIG_AGC2_MAX_GAIN_CHANGE_DB_PER_SECOND, 1e-44, kSmallestPositive, true},
	// -90 以上、0 以下
	{APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, 3, 0, true},
	{APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, 0, 0, false},
	{APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, -90, -90, false},
	{APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, -91, -90, true},
	{APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, -1e40, -90, true},
	// 0 以上、50 未満
	{APM_CONFIG_AGC2_FIXED_GAIN_DB, 60, kAgc2FixedGainDbMax, true},
	{APM_CONFIG_AGC2_FIXED_GAIN_DB, 50, kAgc2FixedGainDbMax, true},
	{APM_CONFIG_AGC2_FIXED_GAIN_DB, 49.95, 49.95, false},
	{APM_CONFIG_AGC2_FIXED_GAIN_DB, -1, 0, true},
	// NaN と無限大は標準の値
	{APM_CONFIG_AGC2_HEADROOM_DB, kNaN, 5, true},
	{APM_CONFIG_AGC2_MAX_GAIN_DB, kInf, 50, true},
	{APM_CONFIG_AGC2_MAX_OUTPUT_NOISE_LEVEL_DBFS, -kInf, -50, true},
};

int failures = 0;

void Fail(const char* what, const char* name, double expected, double actual) {
	std::printf("NG: %s: %s が %g のはずが %g\n", what, name, expected, actual);
	++failures;
}

// float の項目は float に丸めてから比べる。
bool Same(double expected, double actual) {
	return static_cast<float>(expected) == static_cast<float>(actual);
}

const Field& FieldAt(ApmConfigIndex index) {
	for (const Field& field : kFields) {
		if (field.index == index) return field;
	}
	std::printf("NG: 位置 %d が対応表にない\n", index);
	std::exit(1);
}

std::vector<double> DefaultValues() {
	return std::vector<double>(kDefaultConfig, kDefaultConfig + APM_CONFIG_COUNT);
}

// 全項目と、窓口が裏で決めている2つ（自動の持ち上げは AGC2 と一緒、AGC1 のマイクの音量は動かさない）を比べる。
void CheckAllFieldsEqual(const char* what, const Config& config, const std::vector<double>& values) {
	for (const Field& field : kFields) {
		if (!Same(values[field.index], field.read(config))) Fail(what, field.name, values[field.index], field.read(config));
	}
	const auto& agc2 = config.gain_controller2;
	if (agc2.adaptive_digital.enabled != agc2.enabled) {
		Fail(what, "gain_controller2.adaptive_digital.enabled", agc2.enabled, agc2.adaptive_digital.enabled);
	}
	if (config.gain_controller1.analog_gain_controller.enabled) {
		Fail(what, "gain_controller1.analog_gain_controller.enabled", 0, 1);
	}
}

// 対応表に、全部の位置がちょうど1回ずつ出てくるか。
void CheckTableCoversAllIndexes() {
	std::vector<int> seen(APM_CONFIG_COUNT, 0);
	for (const Field& field : kFields) ++seen[field.index];
	for (int i = 0; i < APM_CONFIG_COUNT; ++i) {
		if (seen[i] != 1) {
			std::printf("NG: 対応表で位置 %d が %d 回出てくる\n", i, seen[i]);
			++failures;
		}
	}
}

// 1つの位置だけを変えて入れ、全部の項目が渡した値と一致するか。
void CheckEachIndexReachesItsField(ApmHandle* handle) {
	for (const Field& target : kFields) {
		std::vector<double> values = DefaultValues();
		values[target.index] = target.test_value;
		const int result = apm_apply_config(handle, values.data(), APM_CONFIG_COUNT);
		if (result != 0) Fail("戻り値", target.name, 0, result);
		CheckAllFieldsEqual(target.name, handle->apm->GetConfig(), values);
	}
}

// 範囲の外の値が寄せられ、NaN と無限大が標準の値にされ、直した位置が戻り値に出て、AGC2 が切れないか。
void CheckOutOfRangeValuesAreClamped(ApmHandle* handle) {
	for (const ClampCase& c : kClampCases) {
		const Field& field = FieldAt(c.index);
		std::vector<double> values = DefaultValues();
		values[c.index] = c.input;
		const int result = apm_apply_config(handle, values.data(), APM_CONFIG_COUNT);
		const int expected_result = c.adjusted ? (1 << c.index) : 0;
		if (result != expected_result) Fail("寄せたときの戻り値", field.name, expected_result, result);
		const Config config = handle->apm->GetConfig();
		if (!Same(c.expected, field.read(config))) Fail("寄せた値", field.name, c.expected, field.read(config));
		if (!config.gain_controller2.enabled) Fail("AGC2 が切れた", field.name, 1, 0);
	}
}

// 2つ同時に寄せると両方のビットが立ち、数が違えば（少なくても多くても）何も入れずに -1 が返るか。
void CheckReturnValues(ApmHandle* handle) {
	std::vector<double> values = DefaultValues();
	values[APM_CONFIG_AGC2_HEADROOM_DB] = -3;
	values[APM_CONFIG_AGC1_TARGET_LEVEL_DBFS] = 40;
	const int both = (1 << APM_CONFIG_AGC2_HEADROOM_DB) | (1 << APM_CONFIG_AGC1_TARGET_LEVEL_DBFS);
	const int result = apm_apply_config(handle, values.data(), APM_CONFIG_COUNT);
	if (result != both) Fail("2つ寄せたときの戻り値", "headroom と target", both, result);

	const std::vector<double> defaults = DefaultValues();
	apm_apply_config(handle, defaults.data(), APM_CONFIG_COUNT);
	std::vector<double> changed = DefaultValues();
	changed[APM_CONFIG_AGC2_HEADROOM_DB] = 7.5;
	changed.push_back(0);
	for (int count : {APM_CONFIG_COUNT - 1, APM_CONFIG_COUNT + 1}) {
		const int wrong_count_result = apm_apply_config(handle, changed.data(), count);
		if (wrong_count_result != -1) Fail("数が違うときの戻り値", "count", -1, wrong_count_result);
	}
	CheckAllFieldsEqual("数が違うときは入れない", handle->apm->GetConfig(), defaults);
}

constexpr int kSampleRate = 16000;
constexpr int kFrameSamples = kSampleRate / 100;
constexpr const char* kStatsNames[APM_STATS_COUNT] = {
	"ERL", "ERLE", "delay_ms", "residual_echo_likelihood", "residual_echo_likelihood_recent_max"};

// 鳴らしている音と、その 1/4 の大きさのエコーを録った音として、frames 回（10ms ずつ）流す。
// 2ch 以上なら、全チャンネルに同じ音を交互に並べて渡す。
void RunEcho(ApmHandle* handle, int channels, int frames) {
	std::vector<int16_t> render(kFrameSamples * channels);
	std::vector<int16_t> capture(kFrameSamples * channels);
	for (int frame = 0; frame < frames; ++frame) {
		for (int n = 0; n < kFrameSamples; ++n) {
			const double t = static_cast<double>(frame * kFrameSamples + n) / kSampleRate;
			const auto tone = static_cast<int16_t>(8000 * std::sin(2 * std::numbers::pi * 440 * t));
			for (int ch = 0; ch < channels; ++ch) {
				render[n * channels + ch] = tone;
				capture[n * channels + ch] = static_cast<int16_t>(tone / 4);
			}
		}
		const int render_result = apm_analyze_reverse_stream(handle, render.data());
		const int capture_result = apm_process_stream(handle, capture.data());
		if (render_result != 0) Fail("apm_analyze_reverse_stream の戻り値", "frame", 0, render_result);
		if (capture_result != 0) Fail("apm_process_stream の戻り値", "frame", 0, capture_result);
	}
}

// 3 秒流して、統計の5つが読め、並びが APM の統計の項目と合っているか。
// エコー除去をオフにすると、エコー除去の統計だけが NaN になるか。
void CheckStats(int channels) {
	ApmHandle* handle = apm_create(kSampleRate, channels);
	RunEcho(handle, channels, 300);
	double out[APM_STATS_COUNT];
	apm_get_stats(handle, out);
	const webrtc::AudioProcessingStats& s = handle->latest_stats;
	const double expected[APM_STATS_COUNT] = {
		s.echo_return_loss.value_or(kNaN),
		s.echo_return_loss_enhancement.value_or(kNaN),
		s.delay_ms ? static_cast<double>(*s.delay_ms) : kNaN,
		s.residual_echo_likelihood.value_or(kNaN),
		s.residual_echo_likelihood_recent_max.value_or(kNaN),
	};
	for (int i = 0; i < APM_STATS_COUNT; ++i) {
		if (!std::isfinite(out[i])) Fail("統計が読めない", kStatsNames[i], 0, out[i]);
		if (out[i] != expected[i]) Fail("統計の並び", kStatsNames[i], expected[i], out[i]);
	}

	// エコー除去をオフにすると、エコー除去の統計（0〜2）は NaN になり、エコーの検出器の統計（3〜4）は残る。
	std::vector<double> values = DefaultValues();
	values[APM_CONFIG_ECHO_ENABLED] = 0;
	apm_apply_config(handle, values.data(), APM_CONFIG_COUNT);
	RunEcho(handle, channels, 10);
	apm_get_stats(handle, out);
	for (int i = 0; i < APM_STATS_COUNT; ++i) {
		const bool from_echo_canceller = i <= APM_STATS_DELAY_MS;
		if (from_echo_canceller && !std::isnan(out[i])) Fail("エコー除去がオフなのに残っている", kStatsNames[i], kNaN, out[i]);
		if (!from_echo_canceller && !std::isfinite(out[i])) Fail("エコー除去がオフで消えた", kStatsNames[i], 0, out[i]);
	}
	apm_destroy(handle);
}

// 処理を全部オフにすると、録った音がそのまま返るか（チャンネルの並びが崩れないか）。
// チャンネルごとに違う音を入れ、取り違えも見つける。
void CheckPassThrough(int channels) {
	ApmHandle* handle = apm_create(kSampleRate, channels);
	std::vector<double> values = DefaultValues();
	for (ApmConfigIndex i : {APM_CONFIG_HIGH_PASS_ENABLED, APM_CONFIG_ECHO_ENABLED, APM_CONFIG_NOISE_ENABLED,
			APM_CONFIG_AGC2_ENABLED}) {
		values[i] = 0;
	}
	apm_apply_config(handle, values.data(), APM_CONFIG_COUNT);
	const int total = kFrameSamples * channels;
	std::vector<int16_t> samples(total);
	for (int frame = 0; frame < 50; ++frame) {
		for (int n = 0; n < total; ++n) {
			samples[n] = static_cast<int16_t>((frame * total + n) * 37 % 65536 - 32768);
		}
		const std::vector<int16_t> input = samples;
		apm_process_stream(handle, samples.data());
		for (int n = 0; n < total; ++n) {
			if (samples[n] != input[n]) {
				Fail("全部オフで音が変わった", "sample", input[n], samples[n]);
				apm_destroy(handle);
				return;
			}
		}
	}
	apm_destroy(handle);
}

// ずれの見当の戻り値：0〜500 は 0、外（600、-1）は APM が端の値に直して -13（警告）。
void CheckStreamDelay() {
	ApmHandle* handle = apm_create(kSampleRate, 1);
	const int warning = AudioProcessing::kBadStreamParameterWarning;
	const int in_range = apm_set_stream_delay_ms(handle, 40);
	const int too_large = apm_set_stream_delay_ms(handle, 600);
	const int negative = apm_set_stream_delay_ms(handle, -1);
	if (in_range != 0) Fail("ずれの見当（40）の戻り値", "delay_ms", 0, in_range);
	if (too_large != warning) Fail("ずれの見当（600）の戻り値", "delay_ms", warning, too_large);
	if (negative != warning) Fail("ずれの見当（-1）の戻り値", "delay_ms", warning, negative);
	apm_destroy(handle);
}

}  // namespace

int main() {
	if (apm_create(44100, 1) != nullptr) Fail("対応していないレートで作れた", "44100", 0, 1);
	if (apm_create(kSampleRate, 0) != nullptr) Fail("チャンネルの数 0 で作れた", "num_channels", 0, 1);
	ApmHandle* handle = apm_create(kSampleRate, 1);
	CheckTableCoversAllIndexes();
	CheckEachIndexReachesItsField(handle);
	CheckOutOfRangeValuesAreClamped(handle);
	CheckReturnValues(handle);
	apm_destroy(handle);
	for (int channels : {1, 2}) {
		CheckStats(channels);
		CheckPassThrough(channels);
	}
	CheckStreamDelay();

	if (failures > 0) {
		std::printf("NG: %d 件\n", failures);
		return 1;
	}
	std::printf("OK: %zu 項目の受け渡し、%zu 件の寄せ方、戻り値、音の処理と統計\n", kFields.size(), kClampCases.size());
	return 0;
}
