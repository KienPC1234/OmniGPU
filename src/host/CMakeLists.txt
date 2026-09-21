set(HOST_SOURCES
  buffer_manager.cpp
  cli.cpp
  command_dispatcher.cpp
  config.cpp
  main.cpp
  gpu_manager.cpp
  handshake.cpp
  multi_gpu_compute.cpp
  server.cpp
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

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
install(
  TARGETS omnigpu_host
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Runtime"
)

