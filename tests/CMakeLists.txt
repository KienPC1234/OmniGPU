find_package(GTest CONFIG REQUIRED)

add_executable(omnigpu_tests
  test_host.cpp
  test_guest.cpp
  test_network.cpp
)

target_link_libraries(omnigpu_tests PRIVATE
  omnigpu_common
  omnigpu_guest
  GTest::gtest_main
  GTest::gmock_main
  fmt::fmt
  spdlog::spdlog
)

include(GoogleTest)
gtest_discover_tests(omnigpu_tests)
