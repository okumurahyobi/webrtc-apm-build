cmake_minimum_required(VERSION 3.22)
project(apm_build C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# 静的ライブラリの webrtc_apm を共有ライブラリ（.so）に入れるので、位置に依らないコードで作る。
	# .so から見える名前は、APM_BRIDGE_EXPORT を付けた窓口の関数だけにする
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	set(CMAKE_C_VISIBILITY_PRESET hidden)
	set(CMAKE_CXX_VISIBILITY_PRESET hidden)
	set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../common/apm.cmake")

add_executable(apm_probe apm_probe.cc)
target_link_libraries(apm_probe PRIVATE webrtc_apm)

# ブリッジ（APM を外から使うための C の窓口）
if(APM_ENV STREQUAL "wasm")
	# apm.js と apm.wasm。ES モジュールで、ブラウザのメインスレッド、Worker、AudioWorklet から読める
	set(APM_WASM_EXPORTS
		_apm_create
		_apm_apply_config
		_apm_process_stream
		_apm_analyze_reverse_stream
		_apm_set_stream_delay_ms
		_apm_get_stats
		_apm_destroy
		_malloc
		_free
	)
	list(JOIN APM_WASM_EXPORTS "," APM_WASM_EXPORTS)
	add_executable(apm bridge/apm_bridge.cc)
	target_link_libraries(apm PRIVATE webrtc_apm)
	target_link_options(apm PRIVATE
		-sMODULARIZE
		-sEXPORT_ES6
		-sEXPORT_NAME=createApmModule
		-sENVIRONMENT=web,worker,worklet
		-sALLOW_MEMORY_GROWTH
		"-sEXPORTED_FUNCTIONS=${APM_WASM_EXPORTS}"
		# 呼ぶ側が、確保した場所に音（Int16）と設定・統計（double）を読み書きするため
		"-sEXPORTED_RUNTIME_METHODS=HEAP16,HEAPF64"
	)
	set_target_properties(apm PROPERTIES SUFFIX ".js")
else()
	# Linux は libapm.so。Windows は apm.dll と、つなぐための apm.lib
	add_library(apm SHARED bridge/apm_bridge.cc)
	target_link_libraries(apm PRIVATE webrtc_apm)
	if(APM_ENV STREQUAL "linux")
		# libstdc++ のヘッダーは std の中を見える名前のまま作らせるので、隠す設定だけでは std のテンプレートなどが外に出る。
		# 外に見せる名前を、リンクのときに apm_bridge.map で窓口の関数だけに絞る
		set(APM_BRIDGE_MAP "${CMAKE_CURRENT_LIST_DIR}/bridge/apm_bridge.map")
		target_link_options(apm PRIVATE "-Wl,--version-script=${APM_BRIDGE_MAP}")
		set_target_properties(apm PROPERTIES LINK_DEPENDS "${APM_BRIDGE_MAP}")
	endif()
endif()
