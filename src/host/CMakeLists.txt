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
  multi_gpu_renderer.cpp
  resource_cache.cpp
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
  target_include_directories(omnigpu_host PRIVATE ${FFMPEG_INC_DIR})
  target_link_directories(omnigpu_host PRIVATE ${FFMPEG_LIB_DIR})
  target_link_libraries(omnigpu_host PRIVATE avcodec.lib swscale.lib avutil.lib)
  message(STATUS "FFmpeg: enabled for host encoder")
else()
  message(STATUS "FFmpeg: disabled for host — will fall back to JPEG encoder")
endif()
