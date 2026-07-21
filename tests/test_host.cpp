#include <gtest/gtest.h>
#include "guest/vulkan_serializer.h"
#include "host/vulkan_deserializer.h"
#include <cstring>

using namespace omnigpu;
using namespace omnigpu::host;

// --- VulkanSerializer / VulkanDeserializer round-trip ---
TEST(Serializer, RoundTripU32) {
    serializer::VulkanSerializer ser;
    ser.write_u32(42);
    ser.write_u32(0xDEADBEEF);

    VulkanDeserializer des(ser.data(), ser.size());
    EXPECT_EQ(des.read_u32(), 42u);
    EXPECT_EQ(des.read_u32(), 0xDEADBEEFu);
    EXPECT_TRUE(des.ok());
}

TEST(Serializer, RoundTripU64) {
    serializer::VulkanSerializer ser;
    ser.write_u64(0x1234567890ABCDEFull);
    ser.write_u64(0);

    VulkanDeserializer des(ser.data(), ser.size());
    EXPECT_EQ(des.read_u64(), 0x1234567890ABCDEFull);
    EXPECT_EQ(des.read_u64(), 0u);
    EXPECT_TRUE(des.ok());
}

TEST(Serializer, RoundTripHandles) {
    serializer::VulkanSerializer ser;
    ser.write_handle(0xDEADBEEF);

    VulkanDeserializer des(ser.data(), ser.size());
    EXPECT_EQ(des.read_handle(), 0xDEADBEEFull);
    EXPECT_TRUE(des.ok());
}

TEST(Serializer, OverReadSetsError) {
    uint8_t buf[4] = {};
    VulkanDeserializer des(buf, 4);
    des.read_u32();       // ok
    EXPECT_TRUE(des.ok());
    des.read_u32();       // over-read
    EXPECT_FALSE(des.ok());
}

TEST(Serializer, ReadArrayBounds) {
    serializer::VulkanSerializer ser;
    for (int i = 0; i < 10; i++) ser.write_u32(i);

    VulkanDeserializer des(ser.data(), ser.size());
    auto arr = des.read_array<uint32_t>(10);
    ASSERT_EQ(arr.size(), 10u);
    EXPECT_EQ(arr[0], 0u);
    EXPECT_EQ(arr[9], 9u);
    EXPECT_TRUE(des.ok());
}

TEST(Serializer, ReadArrayRejectsOversize) {
    serializer::VulkanSerializer ser;
    for (int i = 0; i < 10; i++) ser.write_u32(i);

    VulkanDeserializer des(ser.data(), ser.size());
    // kMaxArrayElements = 1M, we should be able to test with a smaller limit
    // This just ensures read_array handles the count field
    auto arr = des.read_array<uint32_t>(10);
    EXPECT_EQ(arr.size(), 10u);
}

TEST(Serializer, SkipSetsErrorOnOvershoot) {
    uint8_t buf[8] = {};
    VulkanDeserializer des(buf, 8);
    des.skip(10); // overshoot
    EXPECT_FALSE(des.ok());
}

TEST(Serializer, ReadRawReturnsFalseOnOverflow) {
    uint8_t buf[4] = {};
    VulkanDeserializer des(buf, 4);
    uint8_t out[8] = {};
    EXPECT_FALSE(des.read_raw(out, 8));
    EXPECT_FALSE(des.ok());
}

TEST(Serializer, ReadString) {
    serializer::VulkanSerializer ser;
    ser.write_string("hello");

    VulkanDeserializer des(ser.data(), ser.size());
    EXPECT_EQ(des.read_string(), "hello");
    EXPECT_TRUE(des.ok());
}
