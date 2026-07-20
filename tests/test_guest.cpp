#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Must include winsock2.h before windows.h to avoid winsock.h conflict
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#undef GetMessage
#undef min
#undef max
#endif

#include <cstring>
#include <vector>

#include "common/flatbuffers_utils.h"
#include "common/gpu_caps.h"
#include "guest/vulkan_serializer.h"
#include "omnigpu_protocol_generated.h"

using namespace omnigpu;

// ============================================================================
// FlatBuffers Protocol Serialization Tests
// ============================================================================

TEST(ProtocolTest, BuildAndParseCommandMessage) {
    uint8_t args[] = {0x01, 0x02, 0x03, 0x04};
    auto builder =
        protocol::build_command(fbs::FunctionId_vkCreateInstance, 42, args, sizeof(args));

    auto vec = protocol::to_vector(builder);
    ASSERT_FALSE(vec.empty());

    auto* msg = protocol::verify_root(vec.data(), vec.size());
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->payload_type(), fbs::MessagePayload_CommandMessage);

    auto* cmd = msg->payload_as_CommandMessage();
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->func_id(), fbs::FunctionId_vkCreateInstance);
    EXPECT_EQ(cmd->request_id(), 42u);

    auto* cmd_args = cmd->args();
    ASSERT_NE(cmd_args, nullptr);
    ASSERT_EQ(cmd_args->size(), 4u);
    EXPECT_EQ(cmd_args->Get(0), 0x01);
    EXPECT_EQ(cmd_args->Get(3), 0x04);
}

TEST(ProtocolTest, BuildAndParseDataMessage) {
    uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    auto builder = protocol::build_data(fbs::DataType_ImageData, 100, payload, sizeof(payload));

    auto vec = protocol::to_vector(builder);
    ASSERT_FALSE(vec.empty());

    auto* msg = protocol::verify_root(vec.data(), vec.size());
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->payload_type(), fbs::MessagePayload_DataMessage);

    auto* data = msg->payload_as_DataMessage();
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->data_type(), fbs::DataType_ImageData);
    EXPECT_EQ(data->data_id(), 100u);

    auto* data_payload = data->payload();
    ASSERT_NE(data_payload, nullptr);
    ASSERT_EQ(data_payload->size(), 3u);
    EXPECT_EQ(data_payload->Get(0), 0xAA);
    EXPECT_EQ(data_payload->Get(2), 0xCC);
}

TEST(ProtocolTest, BuildAndParseBatchMessage) {
    flatbuffers::FlatBufferBuilder fbb;

    auto make_cmd = [&](fbs::FunctionId fid, uint32_t rid, uint8_t val) {
        auto args = fbb.CreateVector(std::vector<uint8_t>{val});
        return fbs::CreateCommandMessage(fbb, fid, rid, args);
    };

    std::vector<flatbuffers::Offset<fbs::CommandMessage>> cmd_offsets;
    cmd_offsets.push_back(make_cmd(fbs::FunctionId_vkQueueSubmit, 1, 0x10));
    cmd_offsets.push_back(make_cmd(fbs::FunctionId_vkCmdDraw, 2, 0x20));
    cmd_offsets.push_back(make_cmd(fbs::FunctionId_vkQueuePresentKHR, 3, 0x30));

    auto cmds_vec = fbb.CreateVector(cmd_offsets);
    auto batch = fbs::CreateBatchMessage(fbb, cmds_vec);
    auto msg = fbs::CreateMessage(fbb, fbs::MessagePayload_BatchMessage, batch.Union());
    fbb.Finish(msg);

    flatbuffers::Verifier verifier(fbb.GetBufferPointer(), fbb.GetSize());
    ASSERT_TRUE(fbs::VerifyMessageBuffer(verifier));

    auto* parsed = fbs::GetMessage(fbb.GetBufferPointer());
    ASSERT_NE(parsed, nullptr);
    ASSERT_EQ(parsed->payload_type(), fbs::MessagePayload_BatchMessage);

    auto* batch_msg = parsed->payload_as_BatchMessage();
    ASSERT_NE(batch_msg, nullptr);
    auto* cmds = batch_msg->commands();
    ASSERT_NE(cmds, nullptr);
    ASSERT_EQ(cmds->size(), 3u);

    EXPECT_EQ(cmds->Get(0)->func_id(), fbs::FunctionId_vkQueueSubmit);
    EXPECT_EQ(cmds->Get(0)->request_id(), 1u);
    EXPECT_EQ(cmds->Get(1)->func_id(), fbs::FunctionId_vkCmdDraw);
    EXPECT_EQ(cmds->Get(2)->func_id(), fbs::FunctionId_vkQueuePresentKHR);
}

// ============================================================================
// VulkanSerializer Tests
// ============================================================================

TEST(SerializerTest, WriteAndReadBasicTypes) {
    serializer::VulkanSerializer ser;

    ser.write_u32(0x12345678);
    ser.write_u64(0xDEADBEEFCAFE);
    ser.write_f32(3.14f);
    ser.write_bool(VK_TRUE);

    ASSERT_EQ(ser.size(), 4 + 8 + 4 + 4);

    const uint8_t* d = ser.data();
    EXPECT_EQ(d[0], 0x78);
    EXPECT_EQ(d[1], 0x56);
}

TEST(SerializerTest, WriteArray) {
    serializer::VulkanSerializer ser;
    int32_t arr[] = {10, 20, 30};
    ser.write_array(arr, 3);
    ASSERT_EQ(ser.size(), 12u);
}

TEST(SerializerTest, WriteHandle) {
    serializer::VulkanSerializer ser;
    ser.write_handle(0xABCD);
    ASSERT_EQ(ser.size(), 8u);
}

// ============================================================================
// GpuCapabilities Tests
// ============================================================================

TEST(GpuCapsTest, DefaultValues) {
    caps::GpuCapabilities caps;
    EXPECT_FALSE(caps.valid());
    EXPECT_EQ(caps.max_push_constants_size, 128u);
    EXPECT_EQ(caps.max_image_dimension_2d, 8192u);
}

TEST(GpuCapsTest, ValidAfterSettingName) {
    caps::GpuCapabilities caps;
    caps.gpu_name = "Test GPU";
    EXPECT_TRUE(caps.valid());
}

// ============================================================================
// Handshake Protocol Tests
// ============================================================================

TEST(HandshakeTest, CapabilitiesRequestRoundtrip) {
    flatbuffers::FlatBufferBuilder fbb;
    auto req = fbs::CreateCapabilitiesRequest(fbb, 1);
    auto msg = fbs::CreateMessage(fbb, fbs::MessagePayload_CapabilitiesRequest, req.Union());
    fbb.Finish(msg);

    auto* parsed = fbs::GetMessage(fbb.GetBufferPointer());
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->payload_type(), fbs::MessagePayload_CapabilitiesRequest);
    EXPECT_EQ(parsed->payload_as_CapabilitiesRequest()->client_version(), 1u);
}

TEST(HandshakeTest, CapabilitiesResponseRoundtrip) {
    flatbuffers::FlatBufferBuilder fbb;
    auto gpu_name = fbb.CreateString("NVIDIA  4090");
    auto resp =
        fbs::CreateCapabilitiesResponse(fbb, gpu_name, 0x1234, VK_API_VERSION_1_3,
                                        8ULL * 1024 * 1024 * 1024, 128, 4, 32, 8192, 1000000);
    auto msg = fbs::CreateMessage(fbb, fbs::MessagePayload_CapabilitiesResponse, resp.Union());
    fbb.Finish(msg);

    auto* parsed = fbs::GetMessage(fbb.GetBufferPointer());
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->payload_type(), fbs::MessagePayload_CapabilitiesResponse);

    auto* caps_resp = parsed->payload_as_CapabilitiesResponse();
    ASSERT_NE(caps_resp, nullptr);
    EXPECT_STREQ(caps_resp->gpu_name()->c_str(), "NVIDIA  4090");
    EXPECT_EQ(caps_resp->api_version(), VK_API_VERSION_1_3);
    EXPECT_EQ(caps_resp->max_memory_allocation(), 8ULL * 1024 * 1024 * 1024);
}
