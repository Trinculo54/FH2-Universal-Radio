cmake_minimum_required(VERSION 3.20)
# Bump VERSION before tagging a release (the tag build checks they match).
project(fh2_universal_radio VERSION 1.3.0 LANGUAGES C CXX RC)
configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/fh2_version.h)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Target is always a Windows PE
if(NOT WIN32)
    message(FATAL_ERROR
        "This project targets Windows. On Linux, configure with a MinGW-w64 "
        "toolchain file, e.g. -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake")
endif()

add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX
                        UNICODE _UNICODE)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /MP /EHsc /wd4324)
    add_compile_options("$<$<CONFIG:Release>:/O2;/GL;/Gy;/Gw>")
    add_link_options("$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF>")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # llvm-mingw (Clang). Match the MSVC profile as closely as makes sense:
    # high warnings, ThinLTO + dead-section stripping in Release, and a
    # statically linked C++ runtime so the resulting DLL has no libc++ /
    # libunwind / libwinpthread dependency at load time.
    # -fms-extensions enables MSVC SEH keywords (__try / __except) that the
    # codebase uses to guard memory probes around game-owned pointers.
    add_compile_options(-fms-extensions)
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unknown-pragmas
                        -Wno-cast-function-type -Wno-language-extension-token
                        -Wno-deprecated-literal-operator)
    add_compile_options("$<$<CONFIG:Release>:-O3;-flto=thin;-ffunction-sections;-fdata-sections>")
    add_link_options("$<$<CONFIG:Release>:-flto=thin;-Wl,--gc-sections;-s>")
    add_link_options(-static)
endif()

# The FH2 Mod Platform (git submodule): d3dcompiler_47.dll, shipped next
# to azimuth.exe, and fh2mod.h.
add_subdirectory(platform)

# The radio itself: fh2-mods\FH2UniversalRadio.dll.
add_library(fh2radio SHARED
    src/proxy/dll_main.cpp
    src/bridge.cpp
    src/log.cpp
    src/config.cpp
    src/config_store.cpp
    src/deps.cpp
    src/playback_dsp.cpp
    src/ring_buffer.cpp
    src/subprocess.cpp
    src/net/http_get.cpp
    src/audio_source_manager.cpp
    src/fh2/game_image.cpp
    src/fh2/settings.cpp
    src/fh2/radio_hook.cpp
    src/fh2/control_loop.cpp
    src/http/http_server.cpp
    src/sources/local_file_source.cpp
    src/sources/youtube_music_source.cpp
    src/sources/jellyfin_source.cpp
    src/sources/external_audio_source.cpp
    src/sources/external_media_session.cpp
    src/sources/spotify_source.cpp
    src/worker/worker_client.cpp
    src/sources/online_radio_source.cpp
    src/version.rc
)
target_compile_definitions(fh2radio PRIVATE FH2_RADIO_VERSION="${PROJECT_VERSION}")

target_include_directories(fh2radio PRIVATE
    include
    ${CMAKE_BINARY_DIR}/generated
    third_party/nlohmann
    third_party/toml11
    third_party/miniaudio
    third_party/stb
)

target_link_libraries(fh2radio PRIVATE fh2mod_api ws2_32 bcrypt psapi winhttp ole32 uuid ksuser runtimeobject)

set_target_properties(fh2radio PROPERTIES
    OUTPUT_NAME FH2UniversalRadio
    PREFIX ""
)

# Worker process: lightweight spawner that lives outside the game's address
# space so CreateProcess (= fork on Wine) is cheap.
add_executable(fh6-radio-worker
    src/worker/worker_main.cpp
    src/subprocess.cpp
    src/log.cpp
    src/net/http_get.cpp
)

target_include_directories(fh6-radio-worker PRIVATE
    include
    third_party/nlohmann
)

target_link_libraries(fh6-radio-worker PRIVATE advapi32 ws2_32 winhttp)

set_target_properties(fh6-radio-worker PROPERTIES
    OUTPUT_NAME fh2-radio-worker
    PREFIX ""
)

# Offline hook test (runs under Wine): maps the real FH2 exe, fakes FMOD.
option(FH2_TESTS "Build the offline FH2 hook test" OFF)
if(FH2_TESTS)
    add_executable(fh2_hook_test
        tests/fh2_hook_test.cpp
        src/fh2/game_image.cpp
        src/fh2/settings.cpp
        src/fh2/radio_hook.cpp
        src/log.cpp
        src/ring_buffer.cpp
        src/audio_source_manager.cpp
    )
    target_include_directories(fh2_hook_test PRIVATE include)
endif()
