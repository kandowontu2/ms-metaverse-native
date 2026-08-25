cmake_minimum_required(VERSION 3.24)

project(ms_metaverse_native VERSION 1.0.1 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "The native port currently targets Windows only")
endif()

option(
    METAVERSE_USE_ORIGINAL_PE_RESOURCES
    "Embed cursors and icon extracted from a lawfully owned MM.EXE"
    OFF
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    @ONLY
)
set(METAVERSE_NATIVE_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
if(METAVERSE_USE_ORIGINAL_PE_RESOURCES)
    set(METAVERSE_ORIGINAL_PE_RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/160.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/161.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/162.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/163.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/164.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/165.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/166.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/174.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cursors/181.cur"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/178.ico"
    )
    set(METAVERSE_ORIGINAL_PE_RESOURCES_COMPLETE TRUE)
    foreach(resource_path IN LISTS METAVERSE_ORIGINAL_PE_RESOURCES)
        if(NOT EXISTS "${resource_path}")
            set(METAVERSE_ORIGINAL_PE_RESOURCES_COMPLETE FALSE)
        endif()
    endforeach()
    if(METAVERSE_ORIGINAL_PE_RESOURCES_COMPLETE)
        list(APPEND METAVERSE_NATIVE_RESOURCES resources.rc)
        message(STATUS "Embedding locally extracted original cursor/icon resources")
    else()
        message(WARNING
            "Original PE resources requested but incomplete; using Windows cursor/icon fallbacks. "
            "Run scripts/prepare-original-resources.ps1 with a lawfully owned MM.EXE."
        )
    endif()
endif()

add_library(metaverse_media STATIC
    src/audio_player.cpp
    src/video_decoder.cpp
)
target_include_directories(metaverse_media
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ffmpeg-static/include"
)
set(FFMPEG_STATIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ffmpeg-static")
foreach(component avformat avcodec avutil swscale swresample)
    set(library_path "${FFMPEG_STATIC_ROOT}/lib/lib${component}.a")
    if(NOT EXISTS "${library_path}")
        message(FATAL_ERROR "Missing bundled static codec library: ${library_path}")
    endif()
    list(APPEND FFMPEG_STATIC_LIBRARIES "${library_path}")
endforeach()
target_link_libraries(metaverse_media PUBLIC
    "-Wl,--start-group"
    ${FFMPEG_STATIC_LIBRARIES}
    "-Wl,--end-group"
    ole32 user32 bcrypt atomic
)
target_compile_definitions(metaverse_media PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(metaverse_data STATIC
    src/category_weight.cpp
    src/comment_order.cpp
    src/game_data.cpp
    src/game_state.cpp
    src/host_interstitial.cpp
    src/host_reaction.cpp
    src/judging_pavilion.cpp
    src/legacy_dialog.cpp
    src/legacy_random.cpp
    src/looks_pavilion.cpp
    src/motion_player.cpp
    src/pavilion_rotation.cpp
    src/profile_store.cpp
    src/scene_navigator.cpp
    src/slot_machine.cpp
)
target_include_directories(metaverse_data PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(ms_metaverse_native WIN32
    src/main.cpp
    src/profile_dialog.cpp
    ${METAVERSE_NATIVE_RESOURCES}
)
target_link_libraries(ms_metaverse_native PRIVATE metaverse_media metaverse_data user32 gdi32 msimg32 winmm shell32)
target_compile_definitions(ms_metaverse_native PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(ms_metaverse_probe
    src/probe_main.cpp
)
target_link_libraries(ms_metaverse_probe PRIVATE metaverse_media metaverse_data)
target_compile_definitions(ms_metaverse_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(ms_metaverse_data_tests
    tests/game_data_tests.cpp
)
target_link_libraries(ms_metaverse_data_tests PRIVATE metaverse_data)
add_test(NAME game_data_parser COMMAND ms_metaverse_data_tests)

if(MINGW)
    target_link_options(ms_metaverse_native PRIVATE -municode)
    foreach(target ms_metaverse_native ms_metaverse_probe ms_metaverse_data_tests)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endforeach()
    foreach(target metaverse_media metaverse_data ms_metaverse_native ms_metaverse_probe ms_metaverse_data_tests)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()
