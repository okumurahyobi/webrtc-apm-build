// wasm で使う。aec3/adaptive_fir_filter_unittest.cc は、テストの中身を x86 か ARM のときだけ定義するのに、まとまりはどの環境でも組み立てる。
// wasm では中身が 0 件になり googletest が失敗として知らせるので、このまとまりは 0 件でよいと伝える

#include <gtest/gtest.h>

namespace webrtc {

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(AdaptiveFirFilterOneTwoFourEightRenderChannels);

}  // namespace webrtc
