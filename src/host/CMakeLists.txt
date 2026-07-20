set(HOST_SOURCES
  adaptive_compressor.cpp
  buffer_manager.cpp
  cli.cpp
  command_dispatcher.cpp
  config.cpp
  ffmpeg_encoder.cpp
  main.cpp
  gpu_manager.cpp
  jpeg_encoder.cpp
  handshake.cpp
  multi_gpu_compute.cpp
  server.cpp
  service_manager.cpp
  video_encoder.cpp
  renderer.cpp
  compressor.cpp
  session.cpp
  vulkan_deserializer.cpp
  vulkan_struct_deserializer.cpp
)

add_executable(omnigpu_host ${HOST_SOURCES})

target_link_libraries(omnigpu_host PRIVATE
  omnigpu_common
)

if(WIN32)
  target_link_libraries(omnigpu_host PRIVATE ws2_32)
  target_compile_options(omnigpu_host PRIVATE /EHsc)
endif()

# FFmpeg for host-side encoding (H264, HEVC, AV1)
if(OMNIGPU_BUILD_FFMPEG AND FFMPEG_FOUND)
  target_compile_definitions(omnigpu_host PRIVATE OMNIGPU_USE_FFMPEG=1)
  if(WIN32)
    target_include_directories(omnigpu_host PRIVATE ${FFMPEG_INC_DIR})
    target_link_directories(omnigpu_host PRIVATE ${FFMPEG_LIB_DIR})
    target_link_libraries(omnigpu_host PRIVATE avcodec.lib swscale.lib avutil.lib)
  else()
    target_link_libraries(omnigpu_host PRIVATE
      PkgConfig::AVCODEC PkgConfig::SWSCALE PkgConfig::AVUTIL)
  endif()
  message(STATUS "FFmpeg: enabled for host encoder")
else()
  message(STATUS "FFmpeg: disabled for host — will fall back to JPEG encoder")
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
install(
  TARGETS omnigpu_host
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Runtime"
)

# Deploy FFmpeg shared DLLs alongside the host executable (Windows only)
if(WIN32 AND OMNIGPU_BUILD_FFMPEG AND DEFINED FFMPEG_BIN_DIR)
  file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
  if(FFMPEG_DLLS)
    install(
      FILES ${FFMPEG_DLLS}
      DESTINATION "${CMAKE_INSTALL_BINDIR}"
      COMPONENT "Runtime"
    )
    # Also copy to build output dir at build time so the host runs in-place
    foreach(_dll ${FFMPEG_DLLS})
      add_custom_command(TARGET omnigpu_host POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${_dll}"
          "$<TARGET_FILE_DIR:omnigpu_host>"
        COMMENT "Deploying FFmpeg DLL: ${_dll}"
      )
    endforeach()
  endif()
endif()

