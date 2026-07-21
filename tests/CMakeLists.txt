find_package(GTest CONFIG REQUIRED)

add_executable(omnigpu_tests
  test_host.cpp
  test_guest.cpp
  test_network.cpp
  ${CMAKE_SOURCE_DIR}/src/guest/vulkan_serializer.cpp
  ${CMAKE_SOURCE_DIR}/src/host/vulkan_deserializer.cpp
)

target_include_directories(omnigpu_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(omnigpu_tests PRIVATE
  omnigpu_common
  GTest::gtest_main
  GTest::gmock_main
  fmt::fmt
  spdlog::spdlog
)

if(WIN32)
  target_link_libraries(omnigpu_tests PRIVATE ws2_32)
endif()

if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(omnigpu_tests PRIVATE /EHsc)
endif()

include(GoogleTest)
gtest_discover_tests(omnigpu_tests)
