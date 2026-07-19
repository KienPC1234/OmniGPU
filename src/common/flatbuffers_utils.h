#pragma once

#include <flatbuffers/flatbuffers.h>
#include "omnigpu_protocol_generated.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace omnigpu::protocol {

using Builder = flatbuffers::FlatBufferBuilder;

Builder build_command(
    fbs::FunctionId func_id,
    uint32_t request_id,
    const uint8_t* args,
    size_t args_size
);

Builder build_data(
    fbs::DataType data_type,
    uint64_t data_id,
    const uint8_t* payload,
    size_t payload_size,
    VkDeviceSize offset = 0
);

std::vector<uint8_t> to_vector(Builder& builder);

const fbs::Message* verify_root(const uint8_t* data, size_t size);

} // namespace omnigpu::protocol
