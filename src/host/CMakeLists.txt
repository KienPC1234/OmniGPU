set(HOST_SOURCES
  main.cpp
  gpu_manager.cpp
  handshake.cpp
  server.cpp
  renderer.cpp
  compressor.cpp
  session.cpp
)

add_executable(omnigpu_host ${HOST_SOURCES})

target_link_libraries(omnigpu_host PRIVATE
  omnigpu_common
)

if(WIN32)
  target_link_libraries(omnigpu_host PRIVATE ws2_32)
endif()
