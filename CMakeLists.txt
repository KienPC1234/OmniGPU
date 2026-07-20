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
if(MSVC)
  add_compile_options(/W3)
elseif(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Enable exception handling for Clang/clang-cl to satisfy clang-tidy
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-fexceptions)
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

# Compile compute shaders to SPIR-V
find_program(GLSLANG_VALIDATOR glslangValidator
    PATHS "C:/VulkanSDK/1.4.350.0/Bin"
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/bin"
)

if(GLSLANG_VALIDATOR)
    set(COMPUTE_SHADERS
        compute_add.comp
        compute_mul.comp
    )
    foreach(SHADER ${COMPUTE_SHADERS})
        set(SPV_OUT "${CMAKE_CURRENT_BINARY_DIR}/${SHADER}.spv")
        list(APPEND COMPUTE_SPVS ${SPV_OUT})
        add_custom_command(
            OUTPUT ${SPV_OUT}
            COMMAND ${GLSLANG_VALIDATOR} -V
                "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/${SHADER}"
                -o ${SPV_OUT}
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/${SHADER}"
            COMMENT "Compiling ${SHADER} to SPIR-V"
        )
    endforeach()
endif()

# Vulkan compute test tool (standalone, uses system Vulkan loader)
add_executable(omnigpu_vk_test src/tools/vk_compute_test.cpp ${COMPUTE_SPVS})
target_include_directories(omnigpu_vk_test PRIVATE
    ${VulkanHeaders_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools
)
target_link_libraries(omnigpu_vk_test PRIVATE Vulkan::Headers Vulkan::Loader)
if(WIN32)
  target_link_libraries(omnigpu_vk_test PRIVATE ws2_32)
endif()

# Compile SPIR-V headers after shaders are built
if(GLSLANG_VALIDATOR)
    add_custom_command(TARGET omnigpu_vk_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "SPIR-V shaders compiled for omnigpu_vk_test"
        COMMENT "Compute test includes ${COMPUTE_SPVS}"
    )
endif()

if(OMNIGPU_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Top-level install rules (targets not covered by subdirectory CMakeLists)
# ---------------------------------------------------------------------------

# Vulkan compute test tool
install(
  TARGETS omnigpu_vk_test
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Tools"
)

# Config files
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/omnigpu_guest.json"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Config"
  OPTIONAL
)

if(OMNIGPU_BUILD_HOST)
  install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/omnigpu_host.json"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT "Config"
    OPTIONAL
  )
endif()

# Mesa3D distribution directory (opengl32.dll + libgallium_wgl.dll)
if(OMNIGPU_FETCH_MESA3D AND OMNIGPU_BUILD_GUEST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mesa3d")
  install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mesa3d/"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/mesa3d"
    COMPONENT "ThirdParty"
    OPTIONAL
  )
endif()

# clvk OpenCL translation layer
if(OMNIGPU_BUILD_GUEST AND DEFINED CLVK_DLL AND EXISTS "${CLVK_DLL}")
  install(
    FILES "${CLVK_DLL}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT "ThirdParty"
  )
  set(CLVK_BIN_DIR "${CMAKE_SOURCE_DIR}/third_party/clvk-bin")
  if(EXISTS "${CLVK_BIN_DIR}/clspv.exe")
    install(
      PROGRAMS "${CLVK_BIN_DIR}/clspv.exe"
      DESTINATION "${CMAKE_INSTALL_BINDIR}"
      COMPONENT "ThirdParty"
    )
  endif()
endif()

