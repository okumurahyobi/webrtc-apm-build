# APM のライブラリ webrtc_apm を作る。build/ と test/ の CMakeLists.txt から、project() のあとに include する。

# 定義：場所、環境、C++ の版
get_filename_component(APM_SRC "${CMAKE_CURRENT_LIST_DIR}/../src" ABSOLUTE)
set(APM_GEN_DIR "${CMAKE_CURRENT_LIST_DIR}/generated")

if(EMSCRIPTEN)
	set(APM_ENV wasm)
elseif(WIN32)
	set(APM_ENV windows)
else()
	set(APM_ENV linux)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ファイルの読み込み：一覧のテキストを読み、src/ からのパスにして返す。# の行と空の行は飛ばす
function(apm_read_list out file)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
	file(STRINGS "${file}" lines ENCODING UTF-8)
	list(FILTER lines EXCLUDE REGEX "^#|^$")
	list(TRANSFORM lines PREPEND "${APM_SRC}/")
	set(${out} "${lines}" PARENT_SCOPE)
endfunction()

apm_read_list(APM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/sources-common.txt")
apm_read_list(APM_ENV_SOURCES "${CMAKE_CURRENT_LIST_DIR}/sources-${APM_ENV}.txt")
list(APPEND APM_SOURCES ${APM_ENV_SOURCES})
set(APM_DEBUG_LIST "${CMAKE_CURRENT_LIST_DIR}/sources-${APM_ENV}-debug.txt")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND EXISTS "${APM_DEBUG_LIST}")
	apm_read_list(APM_DEBUG_SOURCES "${APM_DEBUG_LIST}")
	list(APPEND APM_SOURCES ${APM_DEBUG_SOURCES})
endif()

# 一覧の外で要るもの：api/field_trials_registry.cc が読む registered_field_trials.h。
# 試験用のスイッチ（フィールドトライアル）の名前の一覧。WebRTC はこのヘッダーを src/ に置かず、src/experiments/field_trials.py で書き出す作りになっている
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(APM_FIELD_TRIALS_H "${APM_GEN_DIR}/experiments/registered_field_trials.h")
add_custom_command(
	OUTPUT "${APM_FIELD_TRIALS_H}"
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${APM_GEN_DIR}/experiments"
	COMMAND Python3::Interpreter "${APM_SRC}/experiments/field_trials.py" header --output "${APM_FIELD_TRIALS_H}"
	DEPENDS "${APM_SRC}/experiments/field_trials.py"
	VERBATIM
)

# ライブラリを作る
add_library(webrtc_apm STATIC ${APM_SOURCES} "${APM_FIELD_TRIALS_H}")
target_include_directories(webrtc_apm PUBLIC "${APM_SRC}" "${APM_SRC}/third_party/abseil-cpp" "${APM_GEN_DIR}")
target_compile_definitions(webrtc_apm PUBLIC WEBRTC_APM_DEBUG_DUMP=0)

# 環境ごとのコンパイラの指定。呼ばれない関数は捨てる（wasm のリンカーは指定しなくても捨てる）。Linux と wasm では、捨てないと使わない関数が参照するファイルまで一覧に要る（Windows のリンカーは、捨てる関数の中の参照も探す）
if(APM_ENV STREQUAL "linux")
	target_compile_definitions(webrtc_apm PUBLIC WEBRTC_POSIX WEBRTC_LINUX WEBRTC_ENABLE_AVX2)
	target_compile_options(webrtc_apm PUBLIC -ffunction-sections -fdata-sections)
	target_link_options(webrtc_apm INTERFACE -Wl,--gc-sections)
	# gcc は -mfma があると、別々に書いた掛け算と足し算を1つの命令（FMA）にまとめ、丸め方が変わって C の版と結果がずれる。
	# clang（wasm、Windows）と同じく、まとめさせない
	foreach(source IN LISTS APM_SOURCES)
		if(source MATCHES "_avx2\\.cc$")
			set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-ffp-contract=off")
		endif()
	endforeach()
	# std:: を付けずに isfinite を呼んでいて、gcc の <cmath> だけでは通らないので、math.h を先に読ませる
	set_source_files_properties(
		"${APM_SRC}/modules/audio_processing/echo_detector/mean_variance_estimator.cc"
		"${APM_SRC}/modules/audio_processing/echo_detector/normalized_covariance_estimator.cc"
		PROPERTIES COMPILE_OPTIONS "-include;math.h")
elseif(APM_ENV STREQUAL "wasm")
	target_compile_definitions(webrtc_apm PUBLIC WEBRTC_POSIX)
	target_compile_options(webrtc_apm PUBLIC -msimd128 -Wno-nullability-completeness)
	# README の「高速化」の書き足しをしたときに、pffft が SSE の版で作られるようにする（SSE の命令は Emscripten が WebAssembly の SIMD に置き換える）
	set_source_files_properties("${APM_SRC}/third_party/pffft/src/pffft.c" PROPERTIES COMPILE_OPTIONS "-msse")
elseif(APM_ENV STREQUAL "windows")
	target_compile_definitions(webrtc_apm PUBLIC WEBRTC_WIN NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES WEBRTC_ENABLE_AVX2)
	target_compile_options(webrtc_apm PUBLIC /utf-8 /Gy -Wno-nullability-completeness)
	target_link_options(webrtc_apm INTERFACE /OPT:REF /INCREMENTAL:NO)
	# winmm は時刻とタイマー、user32 は task_queue_win.cc のメッセージの受け渡しに要る
	target_link_libraries(webrtc_apm PUBLIC winmm user32)
	foreach(source IN LISTS APM_SOURCES)
		if(source MATCHES "_avx2\\.cc$")
			set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
		endif()
	endforeach()
endif()
