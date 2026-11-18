// wasm で test/testsupport/file_utils_override.cc の代わりに使う。
// Google のものは実行ファイルの場所から src/ を探すが、node で動かすと実行ファイルが node 本体になる。そのため、ビルドのときに渡す APM_SRC_DIR を src/ とする

#include "test/testsupport/file_utils_override.h"

#include <limits.h>
#include <unistd.h>

#include <string>

#include "absl/strings/string_view.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {
namespace test {
namespace internal {

std::string OutputPath() {
	const std::string path = APM_SRC_DIR "/out";
	if (!CreateDir(path)) {
		return "./";
	}
	return path + "/";
}

std::string WorkingDir() {
	char buffer[PATH_MAX];
	if (getcwd(buffer, sizeof(buffer)) == nullptr) {
		return "./";
	}
	return buffer;
}

std::string ResourcePath(absl::string_view name, absl::string_view extension) {
	return APM_SRC_DIR "/resources/" + std::string(name) + "." + std::string(extension);
}

}  // namespace internal
}  // namespace test
}  // namespace webrtc
