#include "flatbuffers_utils.h"
#include <cstring>
#include <spdlog/spdlog.h>

#ifdef GetMessage
#undef GetMessage
#endif

namespace omnigpu::protocol {

Builder build_command(
    fbs::FunctionId func_id,
    uint32_t request_id,
    const uint8_t* args,
    size_t args_size
) {
    Builder builder;

    auto args_vec = builder.CreateVector(args, args_size);

    auto cmd = fbs::CreateCommandMessage(builder, func_id, request_id, args_vec);

    auto msg = fbs::CreateMessage(builder, fbs::MessagePayload_CommandMessage, cmd.Union());

    builder.Finish(msg);

    return builder;
}

Builder build_data(
    fbs::DataType data_type,
    uint64_t data_id,
    const uint8_t* payload,
    size_t payload_size,
    VkDeviceSize offset
) {
    Builder builder;

    auto payload_vec = builder.CreateVector(payload, payload_size);

    auto data = fbs::CreateDataMessage(builder, data_type, data_id, payload_size, offset, payload_vec);

    auto msg = fbs::CreateMessage(builder, fbs::MessagePayload_DataMessage, data.Union());

    builder.Finish(msg);

    return builder;
}

std::vector<uint8_t> to_vector(Builder& builder) {
    auto span = builder.GetBufferSpan();
    return {span.data(), span.data() + span.size()};
}

const fbs::Message* verify_root(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        SPDLOG_ERROR("verify_root: null data or zero size");
        return nullptr;
    }
    flatbuffers::Verifier verifier(data, size);
    if (!fbs::VerifyMessageBuffer(verifier)) {
        SPDLOG_ERROR("FlatBuffers verification failed");
        return nullptr;
    }
    return fbs::GetMessage(data);
}

} // namespace omnigpu::protocol
