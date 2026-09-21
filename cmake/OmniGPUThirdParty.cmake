# OmniGPU Third-Party Dependency Management
#
# ---------------------------------------------------------------------------
# clvk — OpenCL → Vulkan (built from submodule in third_party/clvk)
# ---------------------------------------------------------------------------
if(OMNIGPU_BUILD_GUEST)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/clvk-bin/OpenCL.dll")
        set(CLVK_DLL "${CMAKE_SOURCE_DIR}/third_party/clvk-bin/OpenCL.dll")
        set(CLVK_AVAILABLE TRUE)
        message(STATUS "clvk: found in third_party/clvk-bin")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/clvk/libOpenCL.so")
        set(CLVK_DLL "${CMAKE_SOURCE_DIR}/third_party/clvk/libOpenCL.so")
        set(CLVK_AVAILABLE TRUE)
        message(STATUS "clvk: found in third_party/clvk")
    else()
        message(STATUS "clvk: not found — place OpenCL.dll in third_party/clvk-bin/ to enable OpenCL forwarding")
    endif()
endif()

# clvk DLLs + clspv.exe — install rules are in the root CMakeLists.txt.

