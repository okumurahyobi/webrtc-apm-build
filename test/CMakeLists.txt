cmake_minimum_required(VERSION 3.22)
project(apm_test C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../common/apm.cmake")

set(APM_RESOURCES_DIR "${APM_SRC}/resources")
set(APM_RESOURCES_URL "https://storage.googleapis.com/chromium-webrtc-resources")

# テストが読む音声データ。src/resources/ の .sha1 の隣に置くのは、WebRTC のテストがそこを探すため
set(APM_RESOURCES
	far8_stereo.pcm
	far16_stereo.pcm
	far32_stereo.pcm
	far48_stereo.pcm
	near8_stereo.pcm
	near16_stereo.pcm
	near32_stereo.pcm
	near48_stereo.pcm
	audio_processing/agc/agc_audio.pcm
	audio_processing/agc/agc_no_circular_buffer.dat
	audio_processing/agc/agc_with_circular_buffer.dat
	audio_processing/agc/agc_pitch_gain.dat
	audio_processing/agc/agc_pitch_lag.dat
	audio_processing/agc/agc_spectral_peak.dat
	audio_processing/agc/agc_vad.dat
	audio_processing/agc/agc_voicing_prob.dat
	audio_processing/agc2/rnn_vad/band_energies.dat
	audio_processing/agc2/rnn_vad/gru_in.dat
	audio_processing/agc2/rnn_vad/pitch_buf_24k.dat
	audio_processing/agc2/rnn_vad/pitch_lp_res.dat
	audio_processing/agc2/rnn_vad/pitch_search_int.dat
	audio_processing/agc2/rnn_vad/samples.pcm
	audio_processing/agc2/rnn_vad/vad_prob.dat
	audio_processing/transient/audio16kHz.pcm
	audio_processing/transient/audio32kHz.pcm
)

# ダウンロード元（APM_RESOURCES_URL）では、ファイルの名前が SHA-1 になっている。すでにあってハッシュが合えば、file(DOWNLOAD) は取りに行かない
foreach(name IN LISTS APM_RESOURCES)
	set(path "${APM_RESOURCES_DIR}/${name}")
	file(STRINGS "${path}.sha1" sha1 LIMIT_COUNT 1)
	if(NOT EXISTS "${path}")
		message(STATUS "音声データを取っています：${name}")
	endif()
	file(DOWNLOAD "${APM_RESOURCES_URL}/${sha1}" "${path}" EXPECTED_HASH SHA1=${sha1})
endforeach()

# googletest が自分の既定（/MT）にせず、APM と同じランタイム（/MD）を使うようにする
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
if(APM_ENV STREQUAL "wasm")
	# APM の wasm 版はスレッドなしで作るので、googletest もスレッドなしにそろえる。そろえないと googletest の中のロックで落ちる
	set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif()
add_subdirectory("${APM_SRC}/third_party/googletest/src" "${CMAKE_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

apm_read_list(APM_TESTS "${CMAKE_CURRENT_LIST_DIR}/tests-common.txt")
apm_read_list(APM_ENV_TESTS "${CMAKE_CURRENT_LIST_DIR}/tests-${APM_ENV}.txt")
list(APPEND APM_TESTS ${APM_ENV_TESTS})

# WebRTC のテストは Chromium の置き場所（testing/gtest/include/...）の名前で googletest を読むので、その名前で本物を読むだけのヘッダーを書き出す
file(CONFIGURE OUTPUT "${APM_GEN_DIR}/testing/gtest/include/gtest/gtest.h" CONTENT "#include <gtest/gtest.h>\n")
file(CONFIGURE OUTPUT "${APM_GEN_DIR}/testing/gtest/include/gtest/gtest-spi.h" CONTENT "#include <gtest/gtest-spi.h>\n")
file(CONFIGURE OUTPUT "${APM_GEN_DIR}/testing/gmock/include/gmock/gmock.h" CONTENT "#include <gmock/gmock.h>\n")

add_executable(apm_tests test_main.cc override/crypto_random.cc ${APM_TESTS})
# WebRTC のテストは実行ファイルの2つ上の resources/ から音声データを探すので、WebRTC と同じく src/out/<環境>/ に出す
set_target_properties(apm_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${APM_SRC}/out/${APM_ENV}")
target_link_libraries(apm_tests PRIVATE webrtc_apm gmock)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	# 型の別名と同じ名前のメンバー関数を gcc はエラーにするので、このチェックだけを止める
	set_source_files_properties("${APM_SRC}/modules/audio_processing/agc2/input_volume_stats_reporter_unittest.cc" PROPERTIES COMPILE_OPTIONS "-Wno-changes-meaning")
endif()
if(APM_ENV STREQUAL "wasm")
	target_sources(apm_tests PRIVATE override/file_utils_override.cc override/gtest_allow_uninstantiated.cc)
	target_compile_definitions(apm_tests PRIVATE "APM_SRC_DIR=\"${APM_SRC}\"")
	# node から音声データをそのまま読み、テストがメモリを多く使っても止まらないようにする。
	# スタックは大きな配列を持つテストがあふれないよう 8MB にし、テストの結果を終了コードで返す
	target_link_options(apm_tests PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sEXIT_RUNTIME=1)
endif()

# ブリッジのテスト。自分で main を持ち、ブリッジの中の APM の設定を読むため apm_bridge.cc を中に取り込む
add_executable(bridge_config_test bridge_config_test.cc)
target_link_libraries(bridge_config_test PRIVATE webrtc_apm)

enable_testing()
add_test(NAME apm_tests COMMAND apm_tests)
add_test(NAME bridge_config_test COMMAND bridge_config_test)
