set(GUEST_SOURCES
  main.cpp
  cache_manager.cpp
  loader.cpp
  vk_intercept.cpp
  vk_intercept_gen.cpp
  vulkan_serializer.cpp
  command_batch.cpp
  client.cpp
)

add_library(omnigpu_guest SHARED ${GUEST_SOURCES})

target_link_libraries(omnigpu_guest PRIVATE
  omnigpu_common
)

if(WIN32)
  target_link_libraries(omnigpu_guest PRIVATE ws2_32)
endif()

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
# Deploy third-party translation DLLs (Zink, clvk) if available
# ---------------------------------------------------------------------------
if(DEFINED ZINK_DLL AND EXISTS "${ZINK_DLL}")
  configure_file("${ZINK_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/opengl32.dll" COPYONLY)
  install(FILES "${ZINK_DLL}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(DEFINED CLVK_DLL AND EXISTS "${CLVK_DLL}")
  configure_file("${CLVK_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/OpenCL.dll" COPYONLY)
  install(FILES "${CLVK_DLL}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

# Copy ICD manifest to output binary dir for runtime loading
add_custom_command(TARGET omnigpu_guest POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_CURRENT_BINARY_DIR}/vk_icd.json"
    "$<TARGET_FILE_DIR:omnigpu_guest>/vk_icd.json"
  COMMENT "Deploying Vulkan ICD manifest..."
)
