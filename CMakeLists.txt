cmake_minimum_required(VERSION 3.28)

# Auto-discover vcpkg toolchain if not explicitly specified
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    message(STATUS "Auto-detected vcpkg toolchain from VCPKG_ROOT: ${CMAKE_TOOLCHAIN_FILE}")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    message(STATUS "Auto-detected local vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
  endif()
endif()

project(OmniGPU
  VERSION 0.1.0
  DESCRIPTION "Seamless Vulkan API Forwarding over LAN"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(GNUInstallDirs)

if(NOT DEFINED CMAKE_CXX_CLANG_TIDY)
  find_program(CLANG_TIDY clang-tidy)
  if(CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY})
  endif()
endif()

# Warning flags — ensure all warnings are visible
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra)
endif()

option(OMNIGPU_BUILD_TESTS "Build tests" ON)
option(OMNIGPU_BUILD_HOST "Build host server" ON)
option(OMNIGPU_BUILD_GUEST "Build guest client" ON)
option(OMNIGPU_FETCH_MESA3D "Download Mesa3D (OpenGL→Vulkan) for guest" ON)
option(OMNIGPU_BUILD_FFMPEG "Download FFmpeg pre-built with HW acceleration" ON)


find_package(Python3 COMPONENTS Interpreter QUIET)

include(cmake/OmniGPUThirdParty.cmake)

find_package(flatbuffers CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanLoader CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(src/common)

if(OMNIGPU_BUILD_HOST)
  add_subdirectory(src/host)
  if(OMNIGPU_BUILD_FFMPEG)
    if(TARGET omnigpu_fetch_ffmpeg)
      add_dependencies(omnigpu_host omnigpu_fetch_ffmpeg)
    endif()
  endif()
endif()

if(OMNIGPU_BUILD_GUEST)
  add_subdirectory(src/guest)
  if(OMNIGPU_FETCH_MESA3D AND TARGET omnigpu_fetch_mesa3d AND NOT MESA3D_AVAILABLE)
    add_dependencies(omnigpu_guest omnigpu_fetch_mesa3d)
  endif()
  if(OMNIGPU_BUILD_FFMPEG)
    if(TARGET omnigpu_fetch_ffmpeg)
      add_dependencies(omnigpu_guest omnigpu_fetch_ffmpeg)
    endif()
  endif()
endif()

# Vulkan compute test tool (standalone, uses system Vulkan loader)
add_executable(omnigpu_vk_test src/tools/vk_compute_test.cpp)
target_include_directories(omnigpu_vk_test PRIVATE ${VulkanHeaders_INCLUDE_DIRS})
target_link_libraries(omnigpu_vk_test PRIVATE Vulkan::Headers Vulkan::Loader)
if(WIN32)
  target_link_libraries(omnigpu_vk_test PRIVATE ws2_32)
endif()

if(OMNIGPU_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
