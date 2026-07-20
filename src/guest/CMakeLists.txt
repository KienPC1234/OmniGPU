set(GUEST_SOURCES
  cache_manager.cpp
  client.cpp
  command_batch.cpp
  dll_main.cpp
  guest_config.cpp
  guest_init.cpp
  icd_entrypoints.cpp
  icd_registry.cpp
  video_decoder.cpp
  vk_intercept.cpp
  vk_intercept_gen.cpp
  vulkan_serializer.cpp
  vulkan_struct_serializer.cpp
)

add_library(omnigpu_guest SHARED ${GUEST_SOURCES})

# Copy full Mesa3D directory to output (when available)
if(EXISTS "${MESA3D_OUTPUT_DIR}")
  add_custom_command(TARGET omnigpu_guest POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${MESA3D_OUTPUT_DIR}"
      "$<TARGET_FILE_DIR:omnigpu_guest>/mesa3d"
    COMMENT "Copying Mesa3D distribution..."
  )
endif()

target_link_libraries(omnigpu_guest PRIVATE
  omnigpu_common
)

if(WIN32)
  target_link_libraries(omnigpu_guest PRIVATE ws2_32)
  target_link_libraries(omnigpu_guest PRIVATE mfplat mfuuid mf ole32)
endif()

# Optional FFmpeg integration for H.264/H.265/AV1 decoding (disabled for Windows guest)
if(WIN32)
  set(OMNIGPU_GUEST_USE_FFMPEG OFF)
else()
  set(OMNIGPU_GUEST_USE_FFMPEG ${OMNIGPU_BUILD_FFMPEG})
endif()

if(OMNIGPU_GUEST_USE_FFMPEG AND FFMPEG_FOUND)
  target_compile_definitions(omnigpu_guest PRIVATE OMNIGPU_USE_FFMPEG=1)
  target_sources(omnigpu_guest PRIVATE ffmpeg_decoder.cpp)
  target_include_directories(omnigpu_guest PRIVATE ${FFMPEG_INC_DIR})
  target_link_directories(omnigpu_guest PRIVATE ${FFMPEG_LIB_DIR})
  target_link_libraries(omnigpu_guest PRIVATE avcodec.lib swscale.lib avutil.lib)
  message(STATUS "FFmpeg: enabled for guest decoder")
else()
  message(STATUS "FFmpeg: disabled for guest — will fall back to built-in decoders")
endif()

# Standalone test client executable
add_executable(omnigpu_guest_test
  main.cpp
  cache_manager.cpp
  guest_config.cpp
  guest_init.cpp
  icd_entrypoints.cpp
  icd_registry.cpp
  video_decoder.cpp
  client.cpp
  command_batch.cpp
  vk_intercept.cpp
  vk_intercept_gen.cpp
  vulkan_serializer.cpp
  vulkan_struct_serializer.cpp
)
target_link_libraries(omnigpu_guest_test PRIVATE omnigpu_common)
if(WIN32)
  target_link_libraries(omnigpu_guest_test PRIVATE ws2_32)
  target_link_libraries(omnigpu_guest_test PRIVATE mfplat mfuuid mf ole32)
endif()
if(OMNIGPU_GUEST_USE_FFMPEG AND FFMPEG_FOUND)
  target_compile_definitions(omnigpu_guest_test PRIVATE OMNIGPU_USE_FFMPEG=1)
  target_sources(omnigpu_guest_test PRIVATE ffmpeg_decoder.cpp)
  target_include_directories(omnigpu_guest_test PRIVATE ${FFMPEG_INC_DIR})
  target_link_directories(omnigpu_guest_test PRIVATE ${FFMPEG_LIB_DIR})
  target_link_libraries(omnigpu_guest_test PRIVATE avcodec.lib swscale.lib avutil.lib)
endif()


# Enable C++ exceptions for cache_manager.cpp (uses nlohmann-json try/catch)
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(omnigpu_guest PRIVATE /EHsc)
  target_compile_options(omnigpu_guest_test PRIVATE /EHsc)
endif()

# ---------------------------------------------------------------------------
# Deploy Vulkan ICD manifest alongside the guest DLL
# ---------------------------------------------------------------------------
set(ICD_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/vk_icd.json")

configure_file(
  "${ICD_MANIFEST}"
  "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
  COPYONLY
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# ---------------------------------------------------------------------------
# Deploy clvk (OpenCL.dll + clspv.exe) alongside the guest intercept
# ---------------------------------------------------------------------------
if(DEFINED CLVK_DLL AND EXISTS "${CLVK_DLL}")
  get_filename_component(CLVK_DLL_NAME "${CLVK_DLL}" NAME)
  configure_file("${CLVK_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/${CLVK_DLL_NAME}" COPYONLY)
  add_custom_command(TARGET omnigpu_guest POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
      "${CMAKE_CURRENT_BINARY_DIR}/${CLVK_DLL_NAME}"
      "$<TARGET_FILE_DIR:omnigpu_guest>/${CLVK_DLL_NAME}"
    COMMENT "Deploying clvk ${CLVK_DLL_NAME}..."
  )

  # Also deploy clspv.exe (required by clvk at runtime)
  set(CLVK_BIN_DIR "${CMAKE_SOURCE_DIR}/third_party/clvk-bin")
  if(EXISTS "${CLVK_BIN_DIR}/clspv.exe")
    configure_file("${CLVK_BIN_DIR}/clspv.exe" "${CMAKE_CURRENT_BINARY_DIR}/clspv.exe" COPYONLY)
    add_custom_command(TARGET omnigpu_guest POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/clspv.exe"
        "$<TARGET_FILE_DIR:omnigpu_guest>/clspv.exe"
      COMMENT "Deploying clspv.exe..."
    )
  endif()
endif()

# Copy ICD manifest to output binary dir for runtime loading
add_custom_command(TARGET omnigpu_guest POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
    "$<TARGET_FILE_DIR:omnigpu_guest>/vk_icd.json"
  COMMENT "Deploying Vulkan ICD manifest..."
)

# ---------------------------------------------------------------------------
# CPack packaging rules for system-level installation
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "OmniGPU-Guest")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OmniGPU Guest ICD for Vulkan forwarding")

# Install ICD manifest to system-visible location
if(WIN32)
  set(ICD_DEST "bin")
else()
  set(ICD_DEST "share/vulkan/icd.d")
endif()

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
  DESTINATION "${ICD_DEST}"
  COMPONENT "ICD"
  RENAME "omnigpu_guest.json"
)

install(
  TARGETS omnigpu_guest
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Runtime"
)

install(
  TARGETS omnigpu_guest_test
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Tools"
  OPTIONAL
)

if(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_NSIS_DISPLAY_NAME "OmniGPU Guest")
else()
  set(CPACK_GENERATOR "TGZ;DEB;RPM")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OmniGPU Team")
  set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libvulkan1")
endif()

include(CPack)
