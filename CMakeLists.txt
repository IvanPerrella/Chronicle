cmake_minimum_required(VERSION 3.20)
project(dcdecomp NONE)

# Allows local candidate builds against an isolated Kazarov checkout.
set(KAZAROV_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools/kazarov" CACHE PATH "Kazarov source tree")
get_filename_component(KAZAROV_SOURCE_DIR "${KAZAROV_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

# PLATFORM picks what is built: PS2 builds and verifies the retail images
# (ps2/CMakeLists.txt), PC builds the x64 port (port/CMakeLists.txt), PSP the
# PSP-2000 port with the pspdev toolchain (docs/PSP.md).
set(PLATFORM PS2 CACHE STRING "Platform to build: PS2 or PC")
set_property(CACHE PLATFORM PROPERTY STRINGS PS2 PC ANDROID PSP)
if(PLATFORM STREQUAL "PS2")
    add_subdirectory(ps2)
elseif(PLATFORM STREQUAL "PC")
    # Here rather than in port so ctest finds the tests from the top of the build tree.
    enable_testing()
    add_subdirectory(port)
elseif(PLATFORM STREQUAL "ANDROID" OR PLATFORM STREQUAL "PSP")
    add_subdirectory(port)
else()
    message(FATAL_ERROR "PLATFORM must be PS2, PC, ANDROID or PSP, not ${PLATFORM}")
endif()
