# OmniGPU Third-Party Dependency Management
#
# Manages fetching Mesa Zink (OpenGL→Vulkan) and clvk (OpenCL→Vulkan)
# pre-built binaries for the guest translation layer.

option(OMNIGPU_FETCH_ZINK "Download Mesa Zink (opengl32.dll) for guest" OFF)
option(OMNIGPU_FETCH_CLVK "Download clvk (OpenCL.dll) for guest" OFF)

# ---------------------------------------------------------------------------
# Zink — OpenGL → Vulkan via Mesa
# ---------------------------------------------------------------------------
if(OMNIGPU_FETCH_ZINK AND OMNIGPU_BUILD_GUEST)
    set(ZINK_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/third_party/zink")
    set(ZINK_DLL "${ZINK_OUTPUT_DIR}/opengl32.dll")

    add_custom_target(omnigpu_fetch_zink
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/third_party/fetch_zink.py"
                --output-dir "${ZINK_OUTPUT_DIR}"
        COMMENT "Fetching Mesa Zink (OpenGL→Vulkan) binaries..."
    )

    if(EXISTS "${ZINK_DLL}")
        set(ZINK_AVAILABLE TRUE)
        message(STATUS "Zink: ${ZINK_DLL} (found)")
    else()
        message(STATUS "Zink: not found — run 'cmake --build . --target omnigpu_fetch_zink' or download manually")
    endif()
endif()

# ---------------------------------------------------------------------------
# clvk — OpenCL → Vulkan
# ---------------------------------------------------------------------------
if(OMNIGPU_FETCH_CLVK AND OMNIGPU_BUILD_GUEST)
    set(CLVK_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/third_party/clvk")

    if(WIN32)
        set(CLVK_DLL "${CLVK_OUTPUT_DIR}/OpenCL.dll")
    else()
        set(CLVK_DLL "${CLVK_OUTPUT_DIR}/libOpenCL.so")
    endif()

    add_custom_target(omnigpu_fetch_clvk
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/third_party/fetch_clvk.py"
                --output-dir "${CLVK_OUTPUT_DIR}"
        COMMENT "Fetching clvk (OpenCL→Vulkan) binaries..."
    )

    if(EXISTS "${CLVK_DLL}")
        set(CLVK_AVAILABLE TRUE)
        message(STATUS "clvk: ${CLVK_DLL} (found)")
    else()
        message(STATUS "clvk: not found — run 'cmake --build . --target omnigpu_fetch_clvk' or download manually")
    endif()
endif()

# ---------------------------------------------------------------------------
# Install rules: deploy third-party DLLs alongside the guest intercept
# ---------------------------------------------------------------------------
if(OMNIGPU_BUILD_GUEST)
    if(DEFINED ZINK_DLL AND EXISTS "${ZINK_DLL}")
        install(FILES "${ZINK_DLL}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
    if(DEFINED CLVK_DLL AND EXISTS "${CLVK_DLL}")
        install(FILES "${CLVK_DLL}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
endif()
