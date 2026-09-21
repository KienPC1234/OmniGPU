set(GUEST_SOURCES
  cache_manager.cpp
  client.cpp
  command_batch.cpp
  dll_main.cpp
  guest_config.cpp
  guest_init.cpp
  icd_entrypoints.cpp
  icd_registry.cpp
  vk_intercept.cpp
  vk_intercept_gen.cpp
  vulkan_serializer.cpp
  vulkan_struct_serializer.cpp
)

add_library(omnigpu_guest SHARED ${GUEST_SOURCES})

target_link_libraries(omnigpu_guest PRIVATE
  omnigpu_common
)

if(WIN32)
  target_compile_definitions(omnigpu_guest PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(omnigpu_guest PRIVATE ws2_32)
endif()

# Standalone test client executable
add_executable(omnigpu_guest_test
  main.cpp
  cache_manager.cpp
  guest_config.cpp
  guest_init.cpp
  icd_entrypoints.cpp
  icd_registry.cpp
  client.cpp
  command_batch.cpp
  vk_intercept.cpp
  vk_intercept_gen.cpp
  vulkan_serializer.cpp
  vulkan_struct_serializer.cpp
)
target_link_libraries(omnigpu_guest_test PRIVATE omnigpu_common)
if(WIN32)
  target_compile_definitions(omnigpu_guest_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(omnigpu_guest_test PRIVATE ws2_32)
endif()


# cache_manager.cpp and guest_config.cpp use nlohmann-json in nothrow mode
# (json::parse with allow_exceptions=false), so no /EHsc / -fexceptions needed.

# ---------------------------------------------------------------------------
# Deploy Vulkan ICD manifest alongside the guest DLL
# ---------------------------------------------------------------------------
set(ICD_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/vk_icd.json")

configure_file(
  "${ICD_MANIFEST}"
  "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
  COPYONLY
)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# ---------------------------------------------------------------------------
# Copy ICD manifest to output binary dir for runtime loading
# ---------------------------------------------------------------------------
add_custom_command(TARGET omnigpu_guest POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
    "$<TARGET_FILE_DIR:omnigpu_guest>/vk_icd.json"
  COMMENT "Deploying Vulkan ICD manifest..."
)

# ---------------------------------------------------------------------------
# CPack packaging rules for system-level installation
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "OmniGPU-Guest")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OmniGPU Guest ICD for Vulkan forwarding")

# Install ICD manifest to system-visible location
if(WIN32)
  set(ICD_DEST "bin")
else()
  set(ICD_DEST "share/vulkan/icd.d")
endif()

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
  DESTINATION "${ICD_DEST}"
  COMPONENT "ICD"
  RENAME "omnigpu_guest.json"
)

install(
  TARGETS omnigpu_guest
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Runtime"
)

install(
  TARGETS omnigpu_guest_test
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT "Tools"
  OPTIONAL
)

if(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_NSIS_DISPLAY_NAME "OmniGPU Guest")
else()
  set(CPACK_GENERATOR "TGZ;DEB;RPM")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OmniGPU Team")
  set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libvulkan1")
endif()

include(CPack)
