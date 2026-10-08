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

# Optional large-format importers (.x/.fbx/.glb/.gltf/.obj).
# Explicit opt-in: normal Android builds remain offline and fast.
option(BUS_ANDROID_WITH_ASSIMP "Build Assimp 5.4.3 importers for Android" OFF)
if(BUS_ANDROID_WITH_ASSIMP)
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_X_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_3DS_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(bus_android_assimp
        URL "https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz"
    )
    FetchContent_MakeAvailable(bus_android_assimp)
    if(TARGET assimp::assimp)
        target_link_libraries(bus_android_app assimp::assimp)
    elseif(TARGET assimp)
        target_link_libraries(bus_android_app assimp)
    else()
        message(FATAL_ERROR "Assimp target unavailable")
    endif()
    target_compile_definitions(bus_android_app PRIVATE BUS_ANDROID_HAS_ASSIMP=1)
endif()

target_link_libraries(bus_android_app GLESv3 android log jnigraphics aaudio)
