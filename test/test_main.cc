// テストの入口。Google の入口（src/test/test_main.cc）は SSL の初期化などで BoringSSL まで要るので、テストに要る初期化だけをする

#include <gmock/gmock.h>

#include <cstdlib>
#include <string_view>
#include <utility>

#include "rtc_base/logging.h"
#include "system_wrappers/include/metrics.h"

namespace {

// ログのテストは、入口でログを初期化してある前提で書かれているので、Google の入口と同じ設定で初期化する。
// WEBRTC_TEST_SKIP_LOGGING_INIT=1 は、初期化そのものを確かめるテストが付けるもので、そのときは何もしない
void InitializeLoggingUnlessSkipped() {
	const char* skip = std::getenv("WEBRTC_TEST_SKIP_LOGGING_INIT");
	if (skip != nullptr && std::string_view(skip) == "1") {
		return;
	}
	webrtc::LoggingConfig config;
	config.set_min_severity(webrtc::LS_WARNING);
	config.set_debug_severity(webrtc::LS_WARNING);
	config.set_log_to_stderr(true);
	webrtc::InitializeLogging(std::move(config));
}

}  // namespace

int main(int argc, char* argv[]) {
	InitializeLoggingUnlessSkipped();
	// 統計の記録を確かめるテストがあるので、記録を有効にする
	webrtc::metrics::Enable();
	testing::InitGoogleMock(&argc, argv);
	return RUN_ALL_TESTS();
}
