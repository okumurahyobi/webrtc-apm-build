// rtc_base/crypto_random.cc の CreateRandomUuid() の代わり。test/testsupport/file_utils.cc が一時ファイルの名前に使う。
// 本物は暗号のライブラリ（BoringSSL）が要るので、テストでは普通の乱数で作る

#include <random>
#include <string>

#include "rtc_base/crypto_random.h"

namespace webrtc {

std::string CreateRandomUuid() {
	constexpr char kHex[] = "0123456789abcdef";
	std::random_device device;
	std::mt19937_64 engine(device());
	std::uniform_int_distribution<int> digit(0, 15);
	// 4 は UUID の版。y は 8〜b のどれか（RFC 4122 の variant の決まり）
	std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
	for (char& c : uuid) {
		if (c == 'x') {
			c = kHex[digit(engine)];
		} else if (c == 'y') {
			c = kHex[8 + digit(engine) % 4];
		}
	}
	return uuid;
}

}  // namespace webrtc
