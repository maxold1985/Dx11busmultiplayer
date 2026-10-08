cmake_minimum_required(VERSION 3.10)
project(DX11BusMultiplayer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_executable(bus_sim_tests tests/simulation_test.cpp)
target_compile_definitions(bus_sim_tests PRIVATE BUS_SIM_TEST)
add_test(NAME bus_simulation COMMAND bus_sim_tests)
add_executable(suspension_steering_tests tests/suspension_steering_test.cpp)
target_compile_definitions(suspension_steering_tests PRIVATE BUS_SIM_TEST)
add_test(NAME suspension_steering COMMAND suspension_steering_tests)
add_executable(omsi_format_tests tests/omsi_format_test.cpp)
add_test(NAME omsi_formats COMMAND omsi_format_tests)
add_executable(native_3ds_tests tests/3ds_format_test.cpp)
add_test(NAME native_3ds_formats COMMAND native_3ds_tests)
if(NOT WIN32)
    message(STATUS "Non-Windows: physics tests only")
    return()
endif()
add_executable(bus_server src/server.cpp)
target_link_libraries(bus_server ws2_32)
option(BUS_WITH_ASSIMP "Enable GLB/FBX/3DS via compatible i686 Assimp" OFF)
set(BUS_ASSIMP_INCLUDE_DIR "" CACHE PATH "Directory containing assimp/Importer.hpp")
set(BUS_ASSIMP_LIBRARY "" CACHE FILEPATH "Full path to i686 Assimp .a or .dll.a")
if(BUS_WITH_ASSIMP)
    # Prefer explicit paths. Broken build-tree assimpConfig.cmake files must not
    # be loaded: they often refer to a missing generated assimpTargets.cmake.
    if(BUS_ASSIMP_INCLUDE_DIR AND BUS_ASSIMP_LIBRARY)
        if(NOT EXISTS "${BUS_ASSIMP_INCLUDE_DIR}/assimp/Importer.hpp")
            message(FATAL_ERROR "Invalid BUS_ASSIMP_INCLUDE_DIR: expected assimp/Importer.hpp")
        endif()
        if(NOT EXISTS "${BUS_ASSIMP_LIBRARY}")
            message(FATAL_ERROR "BUS_ASSIMP_LIBRARY does not exist")
        endif()
        add_library(bus_assimp_imported UNKNOWN IMPORTED)
        set_target_properties(bus_assimp_imported PROPERTIES
            IMPORTED_LOCATION "${BUS_ASSIMP_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BUS_ASSIMP_INCLUDE_DIR}")
        set(BUS_ASSIMP_TARGET bus_assimp_imported)
    else()
        # Search actual headers and libraries, never a potentially stale
        # assimpConfig.cmake. ASSIMP_ROOT is an optional installation prefix.
        find_path(BUS_ASSIMP_HEADER_DIR assimp/Importer.hpp
            HINTS "${ASSIMP_ROOT}" "$ENV{assimp_ROOT}"
            PATH_SUFFIXES include)
        find_library(BUS_ASSIMP_FOUND_LIBRARY NAMES assimp libassimp
            HINTS "${ASSIMP_ROOT}" "$ENV{assimp_ROOT}"
            PATH_SUFFIXES lib lib32 build-i686/lib build-i686/bin)
        if(NOT BUS_ASSIMP_HEADER_DIR OR NOT BUS_ASSIMP_FOUND_LIBRARY)
            message(FATAL_ERROR
                "Assimp i686 headers/library not found. Set "
                "-DBUS_ASSIMP_INCLUDE_DIR=.../include and "
                "-DBUS_ASSIMP_LIBRARY=.../libassimp.dll.a (or libassimp.a). "
                "Do not set assimp_DIR to an incomplete build directory.")
        endif()
        add_library(bus_assimp_imported UNKNOWN IMPORTED)
        set_target_properties(bus_assimp_imported PROPERTIES
            IMPORTED_LOCATION "${BUS_ASSIMP_FOUND_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BUS_ASSIMP_HEADER_DIR}")
        set(BUS_ASSIMP_TARGET bus_assimp_imported)
    endif()
endif()
find_program(FXC_EXECUTABLE NAMES fxc fxc.exe)
if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR "fxc.exe not found: set FXC_EXECUTABLE")
endif()
set(VS_CSO "${CMAKE_CURRENT_BINARY_DIR}/bus_vs.cso")
set(PS_CSO "${CMAKE_CURRENT_BINARY_DIR}/bus_ps.cso")
add_custom_command(OUTPUT "${VS_CSO}"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T vs_4_0 /E VSMain /Fo "${VS_CSO}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bus.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bus.hlsl" VERBATIM)
add_custom_command(OUTPUT "${PS_CSO}"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T ps_4_0 /E PSMain /Fo "${PS_CSO}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bus.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bus.hlsl" VERBATIM)
add_custom_target(bus_shaders ALL DEPENDS "${VS_CSO}" "${PS_CSO}")
add_executable(bus_client WIN32 src/client.cpp)
add_dependencies(bus_client bus_shaders)
target_link_libraries(bus_client d3d11 dxgi ws2_32 user32 gdi32 winmm ole32 uuid windowscodecs comdlg32)
if(BUS_WITH_ASSIMP)
    target_compile_definitions(bus_client PRIVATE BUS_HAS_ASSIMP=1)
    target_link_libraries(bus_client ${BUS_ASSIMP_TARGET})
endif()
add_custom_command(TARGET bus_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VS_CSO}" "$<TARGET_FILE_DIR:bus_client>/bus_vs.cso"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PS_CSO}" "$<TARGET_FILE_DIR:bus_client>/bus_ps.cso"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:bus_client>/assets")
