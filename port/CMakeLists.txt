cmake_minimum_required(VERSION 3.24)
project(OpenGangstarWindows LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure the port for 64-bit Windows. Use -A x64 with Visual Studio.")
endif()
if(MSVC AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "The native port requires the AMD64 toolchain. Use -A x64.")
endif()

find_path(VULKAN_HEADERS vulkan/vulkan.h HINTS "$ENV{VULKAN_SDK}/Include" REQUIRED)
find_program(GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(city_shaders)
foreach(stage vert frag)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/city.${stage}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/city_${stage}.inc")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${GLSLC}" --target-env=vulkan1.0 -mfmt=c "${source}" -o "${output}"
        DEPENDS "${source}" VERBATIM)
    list(APPEND city_shaders "${output}")
endforeach()
set(WCH_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../games/gangstar-wch/src/game")
if(NOT EXISTS "${WCH_SOURCE}/persistence/StreamReader.cpp")
    message(FATAL_ERROR "Initialize the gangstar-wch submodule before configuring the port.")
endif()

include(cmake/GameSources.cmake)

include(FetchContent)
set(ZLIB_BUILD_SHARED OFF)
set(ZLIB_BUILD_TESTING OFF)
set(ZLIB_INSTALL OFF)
FetchContent_Declare(zlib
    URL https://zlib.net/fossils/zlib-1.4.tar.gz
    URL_HASH SHA256=02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(zlib)
include(cmake/GameSWF.cmake)
include(cmake/Vorbis.cmake)
include(cmake/Box2D.cmake)

add_library(opengangstar_assets STATIC
    city_assets.cpp
    city_particles.cpp
    filesystem.cpp
    retail_audio.cpp
    retail_radio.cpp
    retail_save.cpp
    retail_achievements.cpp
    retail_constants.cpp
    retail_sprite.cpp
    retail_minimap.cpp
    retail_mission.cpp
    texture.cpp
    vorbis_audio.cpp
    "${WCH_SOURCE}/persistence/FileSystemBase.cpp"
    "${WCH_SOURCE}/persistence/StreamReader.cpp")
target_compile_features(opengangstar_assets PUBLIC cxx_std_17)
target_include_directories(opengangstar_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${WCH_SOURCE}/persistence")
target_link_libraries(opengangstar_assets PRIVATE ole32 shell32 uuid windowscodecs vorbisfile winmm)

add_executable(gangstar-wch
    city_view.cpp
    intro_movie.cpp
    main.cpp
    menu_font.cpp
    menu_raster.cpp
    menu_runtime.cpp
    menu_swf.cpp
    startup_logo.cpp
    vulkan.cpp
    win32_touch.cpp
    ${city_shaders}
    "${WIN32_GAME_SOURCE}/ai/StateMachine.cpp"
    "${WIN32_GAME_SOURCE}/input/TouchScreenBase.cpp"
)
target_compile_features(gangstar-wch PRIVATE cxx_std_17)
target_compile_definitions(gangstar-wch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR)
target_include_directories(gangstar-wch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${VULKAN_HEADERS}" "${WIN32_GAME_SOURCE}/ai"
    "${WIN32_GAME_SOURCE}/input" "${WCH_SOURCE}/persistence")
target_link_libraries(gangstar-wch PRIVATE gdi32 mfplat mfreadwrite mfuuid ole32 user32 winmm xinput9_1_0
    ZLIB::ZLIBSTATIC gameswf_core opengangstar_assets opengangstar_box2d)

include(CTest)
if(BUILD_TESTING)
    add_executable(box2d_test tests/box2d.cpp)
    target_link_libraries(box2d_test PRIVATE opengangstar_box2d)
    add_test(NAME box2d COMMAND box2d_test)

    add_executable(win32_input_test tests/win32_input.cpp win32_touch.cpp
        "${WIN32_GAME_SOURCE}/input/TouchScreenBase.cpp")
    target_compile_features(win32_input_test PRIVATE cxx_std_17)
    target_compile_definitions(win32_input_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(win32_input_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${WIN32_GAME_SOURCE}/input")
    target_link_libraries(win32_input_test PRIVATE user32 xinput9_1_0)
    add_test(NAME win32_input COMMAND win32_input_test)

    add_executable(city_collision_test tests/city_collision.cpp city_view.cpp)
    target_link_libraries(city_collision_test PRIVATE opengangstar_assets)
    add_test(NAME city_collision COMMAND city_collision_test)

    add_executable(texture_png_test tests/texture_png.cpp)
    target_link_libraries(texture_png_test PRIVATE opengangstar_assets)
    add_test(NAME texture_png COMMAND texture_png_test)

    add_executable(retail_asset_audit tests/retail_asset_audit.cpp)
    target_link_libraries(retail_asset_audit PRIVATE opengangstar_assets)

    add_executable(retail_audio_test tests/retail_audio.cpp)
    target_link_libraries(retail_audio_test PRIVATE opengangstar_assets)

    add_executable(retail_radio_test tests/retail_radio.cpp)
    target_link_libraries(retail_radio_test PRIVATE opengangstar_assets)
    add_test(NAME retail_radio COMMAND retail_radio_test)

    add_executable(retail_save_test tests/retail_save.cpp)
    target_link_libraries(retail_save_test PRIVATE opengangstar_assets)
    add_test(NAME retail_save COMMAND retail_save_test)
endif()

if(MSVC)
    foreach(target opengangstar_assets gangstar-wch gameswf_core)
        target_compile_options(${target} PRIVATE /we4302 /we4311 /we4312)
    endforeach()
endif()
