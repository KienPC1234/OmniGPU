set(GUEST_SOURCES
  cache_manager.cpp
  client.cpp
  command_batch.cpp
  dll_main.cpp
  guest_config.cpp
  guest_init.cpp
  guest_ipc.cpp
  icd_entrypoints.cpp
  icd_registry.cpp
  loader.cpp
  resource_tracker.cpp
  video_decoder.cpp
  vk_intercept.cpp
  vk_intercept_gen.cpp
  vulkan_serializer.cpp
  vulkan_struct_serializer.cpp
)

add_library(omnigpu_guest SHARED ${GUEST_SOURCES})

target_link_libraries(omnigpu_guest PRIVATE
  omnigpu_common
)

if(WIN32)
  target_link_libraries(omnigpu_guest PRIVATE ws2_32)
  target_link_libraries(omnigpu_guest PRIVATE mfplat mfuuid mf ole32)
endif()

# Optional FFmpeg integration for H.264/H.265/AV1 decoding
if(OMNIGPU_BUILD_FFMPEG AND FFMPEG_FOUND)
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
# Guest daemon (background pre-connect + IPC server)
add_executable(omnigpu_guestd
  guest_daemon.cpp
  guest_ipc.cpp
  guest_config.cpp
)
target_link_libraries(omnigpu_guestd PRIVATE omnigpu_common)
if(WIN32)
  target_link_libraries(omnigpu_guestd PRIVATE ws2_32)
endif()
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(omnigpu_guestd PRIVATE /EHsc)
endif()

add_executable(omnigpu_guest_test
  main.cpp
  cache_manager.cpp
  guest_config.cpp
  guest_init.cpp
  guest_ipc.cpp
  icd_registry.cpp
  loader.cpp
  resource_tracker.cpp
  video_decoder.cpp
  client.cpp
  command_batch.cpp
  vk_intercept_gen.cpp
  vulkan_serializer.cpp
  vulkan_struct_serializer.cpp
)
target_link_libraries(omnigpu_guest_test PRIVATE omnigpu_common)
if(WIN32)
  target_link_libraries(omnigpu_guest_test PRIVATE ws2_32)
  target_link_libraries(omnigpu_guest_test PRIVATE mfplat mfuuid mf ole32)
endif()
if(OMNIGPU_BUILD_FFMPEG AND FFMPEG_FOUND)
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
# Deploy third-party translation DLLs (Zink, clvk) if available
# ---------------------------------------------------------------------------
# Helper: copy a DLL into the guest output directory
function(deploy_zink_dll VAR_NAME BIN_NAME)
  if(DEFINED ${VAR_NAME} AND EXISTS "${${VAR_NAME}}")
    configure_file("${${VAR_NAME}}" "${CMAKE_CURRENT_BINARY_DIR}/${BIN_NAME}" COPYONLY)
    add_custom_command(TARGET omnigpu_guest POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/${BIN_NAME}"
        "$<TARGET_FILE_DIR:omnigpu_guest>/${BIN_NAME}"
      COMMENT "Deploying ${BIN_NAME}..."
    )
  endif()
endfunction()

# Zink 64-bit core DLLs
deploy_zink_dll(ZINK_DLL "opengl32.dll")
deploy_zink_dll(ZINK_GALLIUM_WGL "libgallium_wgl.dll")

# Zink 32-bit DLLs → x86/ subfolder
if(DEFINED ZINK_DLL_X86 AND EXISTS "${ZINK_DLL_X86}")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/x86")
  configure_file("${ZINK_DLL_X86}" "${CMAKE_CURRENT_BINARY_DIR}/x86/opengl32.dll" COPYONLY)
  add_custom_command(TARGET omnigpu_guest POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
      "${CMAKE_CURRENT_BINARY_DIR}/x86/opengl32.dll"
      "$<TARGET_FILE_DIR:omnigpu_guest>/x86/opengl32.dll"
    COMMENT "Deploying Zink 32-bit opengl32.dll..."
  )
  deploy_zink_dll(ZINK_GALLIUM_WGL_X86 "x86/libgallium_wgl.dll")
endif()

if(DEFINED CLVK_DLL AND EXISTS "${CLVK_DLL}")
  get_filename_component(CLVK_DLL_NAME "${CLVK_DLL}" NAME)
  configure_file("${CLVK_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/${CLVK_DLL_NAME}" COPYONLY)
  add_custom_command(TARGET omnigpu_guest POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
      "${CMAKE_CURRENT_BINARY_DIR}/${CLVK_DLL_NAME}"
      "$<TARGET_FILE_DIR:omnigpu_guest>/${CLVK_DLL_NAME}"
    COMMENT "Deploying clvk ${CLVK_DLL_NAME}..."
  )
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
