cmake_minimum_required(VERSION 3.21)

project(MicroKeys LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# src/version.h is the one place the version lives
file(STRINGS "${CMAKE_SOURCE_DIR}/src/version.h" _version_line REGEX "#define VERSION_TEXT[ \t]+\"")
string(REGEX MATCH "\"([0-9.]+)\"" _unused "${_version_line}")
set(MICROKEYS_VERSION "${CMAKE_MATCH_1}")
message(STATUS "MicroKeys version ${MICROKEYS_VERSION}")

if(MSVC)
	add_compile_options(/utf-8 /W3 /permissive-)
endif()

# The platform neutral core, built everywhere including Linux
add_library(mkcore STATIC
	src/core/Image.cpp
	src/core/Text.cpp
	src/core/Triggers.cpp
	src/core/UpdateCore.c
)
target_include_directories(mkcore PUBLIC src/core src)

enable_testing()
add_executable(core_tests tests/core/test_core.cpp tests/core/test_updatecore.cpp)
target_link_libraries(core_tests PRIVATE mkcore)
add_test(NAME core_tests COMMAND core_tests)

if(WIN32)
	if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
		set(MK_ARCH "arm64")
	else()
		set(MK_ARCH "x64")
	endif()
	message(STATUS "Building for ${MK_ARCH}")

	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	execute_process(
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/fetch_python.py" --arch ${MK_ARCH} --dest "${CMAKE_BINARY_DIR}/deps"
		OUTPUT_VARIABLE MK_PYROOT
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE _fetch_result
	)
	if(NOT _fetch_result EQUAL 0)
		message(FATAL_ERROR "Unable to fetch the Python runtime")
	endif()
	file(READ "${CMAKE_SOURCE_DIR}/tools/python_version.json" _pins)
	string(JSON MK_PYVER GET "${_pins}" version)
	string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _unused "${MK_PYVER}")
	set(MK_PYLIB "python${CMAKE_MATCH_1}${CMAKE_MATCH_2}")

	set(MK_WIN_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)

	add_executable(MicroKeysApp WIN32
		src/core/ScriptHost.cpp
		src/core/PyModule.cpp
		src/platform/win/WinMain.cpp
		src/platform/win/WinApp.cpp
		src/platform/win/WinPlatform.cpp
		src/platform/win/WinClipboard.cpp
		src/platform/win/WinKeys.cpp
		src/platform/win/WinText.cpp
		src/platform/win/WinOverlay.cpp
		src/platform/win/WinScreen.cpp
		src/platform/win/WinOcr.cpp
		src/platform/win/WinAutomation.cpp
		src/platform/win/WinDiscovery.cpp
		src/platform/win/MicroKeys.rc
	)
	target_compile_definitions(MicroKeysApp PRIVATE ${MK_WIN_DEFS} MK_PYLIB="${MK_PYLIB}" MK_PYVER="${MK_PYVER}")
	# With MICROKEYS_UPDATES off there's no updater at all: no code, no
	# setting, and nothing about updates in the docs
	option(MICROKEYS_UPDATES "Build in the self-updater" ON)
	if(MICROKEYS_UPDATES)
		target_sources(MicroKeysApp PRIVATE src/platform/win/WinUpdate.cpp)
		set(MK_DOCS_FLAGS "")
	else()
		message(STATUS "Building without the updater")
		target_compile_definitions(MicroKeysApp PRIVATE MK_NO_UPDATES)
		set(MK_DOCS_FLAGS --no-updates)
	endif()
	target_include_directories(MicroKeysApp PRIVATE "${MK_PYROOT}/sdk/include" src/platform/win)
	target_link_directories(MicroKeysApp PRIVATE "${MK_PYROOT}/sdk/libs")
	target_link_libraries(MicroKeysApp PRIVATE mkcore ${MK_PYLIB}
		user32 gdi32 shell32 comctl32 comdlg32 winhttp bcrypt dwmapi shcore ole32 oleaut32 advapi32 shlwapi version windowscodecs
		dnsapi ws2_32 taskschd runtimeobject)
	target_link_options(MicroKeysApp PRIVATE /MANIFEST:NO)

	add_executable(MicroKeys WIN32
		src/platform/win/Launcher.cpp
		src/platform/win/Launcher.rc
	)
	target_compile_definitions(MicroKeys PRIVATE ${MK_WIN_DEFS})
	target_link_libraries(MicroKeys PRIVATE mkcore user32 shell32 shlwapi)
	target_link_options(MicroKeys PRIVATE /MANIFEST:NO)

	# Lay the build out the way a release unpacks:
	#   stage/<config>/MicroKeys/MicroKeys.exe                 the launcher
	#   stage/<config>/MicroKeys/app-<version>/MicroKeysApp.exe plus the Python runtime
	#   stage/<config>/MicroKeys/app-<version>/lib/microkeys    the Python package,
	#     which with lib/pyproject.toml also installs with "pip install -e"
	set(MK_STAGE "${CMAKE_BINARY_DIR}/stage/$<CONFIG>/MicroKeys")
	file(WRITE "${CMAKE_BINARY_DIR}/VERSION" "${MICROKEYS_VERSION}\n")
	set(MK_STAGE_APP "${MK_STAGE}/app-${MICROKEYS_VERSION}")
	add_custom_command(TARGET MicroKeysApp POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MK_STAGE_APP}/lib"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${MK_PYROOT}/runtime" "${MK_STAGE_APP}"
		COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MicroKeysApp>" "${MK_STAGE_APP}/"
	)
	# The Python package and the docs change without the exe relinking, so
	# they're staged on every build
	add_custom_target(MicroKeysPython ALL
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MK_STAGE_APP}/lib"
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${MK_STAGE_APP}/lib/microkeys"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/python/microkeys" "${MK_STAGE_APP}/lib/microkeys"
		COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/python/_microkeys.pyi" "${CMAKE_SOURCE_DIR}/python/pyproject.toml"
			"${CMAKE_SOURCE_DIR}/python/_build_backend.py" "${CMAKE_BINARY_DIR}/VERSION" "${MK_STAGE_APP}/lib/"
		COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/gen_docs.py" --out "${MK_STAGE_APP}/docs" ${MK_DOCS_FLAGS}
	)
	add_dependencies(MicroKeysPython MicroKeysApp)
	add_custom_command(TARGET MicroKeys POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MK_STAGE}"
		COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MicroKeys>" "${MK_STAGE}/"
	)
endif()
