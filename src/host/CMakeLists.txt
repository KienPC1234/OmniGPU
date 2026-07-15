set(HOST_SOURCES
  adaptive_compressor.cpp
  amf_encoder.cpp
  cli.cpp
  command_dispatcher.cpp
  config.cpp
  main.cpp
  gpu_manager.cpp
  jpeg_encoder.cpp
  handshake.cpp
  multi_gpu_renderer.cpp
  nvenc_encoder.cpp
  resource_cache.cpp
  server.cpp
  service_manager.cpp
  vaapi_encoder.cpp
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

target_include_directories(omnigpu_host PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/AMF/include"
)
if(WIN32)
  target_link_libraries(omnigpu_host PRIVATE ws2_32)
  target_compile_options(omnigpu_host PRIVATE /EHsc)
endif()
