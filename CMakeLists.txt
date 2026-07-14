cmake_minimum_required(VERSION 3.28)

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

option(OMNIGPU_BUILD_TESTS "Build tests" ON)
option(OMNIGPU_BUILD_HOST "Build host server" ON)
option(OMNIGPU_BUILD_GUEST "Build guest client" ON)
option(OMNIGPU_FETCH_ZINK "Download Mesa Zink (OpenGL→Vulkan) for guest" OFF)
option(OMNIGPU_FETCH_CLVK "Download clvk (OpenCL→Vulkan) for guest" OFF)

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

add_subdirectory(src/common)

if(OMNIGPU_BUILD_HOST)
  add_subdirectory(src/host)
endif()

if(OMNIGPU_BUILD_GUEST)
  add_subdirectory(src/guest)
endif()

if(OMNIGPU_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
