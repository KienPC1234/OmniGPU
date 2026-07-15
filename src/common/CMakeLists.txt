set(FBS_SCHEMAS
  ${CMAKE_CURRENT_SOURCE_DIR}/../schemas/omnigpu_protocol.fbs
)

set(FBS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/schemas)
set(FBS_GENERATED_HEADER ${FBS_GENERATED_DIR}/omnigpu_protocol_generated.h)

add_custom_command(
  OUTPUT ${FBS_GENERATED_HEADER}
  COMMAND flatbuffers::flatc --cpp --gen-object-api
          -o ${FBS_GENERATED_DIR}
          ${FBS_SCHEMAS}
  DEPENDS ${FBS_SCHEMAS}
  COMMENT "Generating FlatBuffers C++ headers from .fbs schemas..."
)

add_library(omnigpu_common STATIC
  flatbuffers_utils.cpp
  gpu_caps_store.cpp
  logger.cpp
  network_utils.cpp
)

# Ensure generated header exists before compiling
add_custom_target(omnigpu_fbs_generated DEPENDS ${FBS_GENERATED_HEADER})
add_dependencies(omnigpu_common omnigpu_fbs_generated)

target_include_directories(omnigpu_common
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
  PUBLIC ${FBS_GENERATED_DIR}
)

target_link_libraries(omnigpu_common PUBLIC
  fmt::fmt
  spdlog::spdlog
  nlohmann_json::nlohmann_json
  Vulkan::Headers
  Vulkan::Loader
)

target_link_libraries(omnigpu_common PUBLIC
  flatbuffers::flatbuffers
  lz4::lz4
  $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)
