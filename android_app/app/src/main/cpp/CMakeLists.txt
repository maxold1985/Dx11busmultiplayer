cmake_minimum_required(VERSION 3.18)
project(DX11BusAndroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(BUS_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../" ABSOLUTE)

add_library(bus_android_app SHARED
    native_bridge.cpp
    android_audio.cpp
    "${BUS_REPO_ROOT}/third_party/stb_vorbis.c"
)
target_compile_definitions(bus_android_app PRIVATE BUS_SIM_TEST=1)
target_include_directories(bus_android_app PRIVATE "${BUS_REPO_ROOT}/src")
target_link_libraries(bus_android_app GLESv3 android log jnigraphics aaudio)
