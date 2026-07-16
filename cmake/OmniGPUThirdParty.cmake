# OmniGPU Third-Party Dependency Management
#
# Manages fetching Mesa3D (OpenGL→Vulkan) pre-built binaries
# and integrating clvk (OpenCL→Vulkan) built from submodule.

# ---------------------------------------------------------------------------
# Mesa3D — OpenGL → Vulkan via Mesa
# ---------------------------------------------------------------------------
if(OMNIGPU_FETCH_MESA3D AND OMNIGPU_BUILD_GUEST)
    set(MESA3D_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/third_party/mesa3d")

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(ZINK_DLL "${MESA3D_OUTPUT_DIR}/x86/opengl32.dll")
        set(ZINK_GALLIUM_WGL "${MESA3D_OUTPUT_DIR}/x86/libgallium_wgl.dll")
    else()
        set(ZINK_DLL "${MESA3D_OUTPUT_DIR}/x64/opengl32.dll")
        set(ZINK_GALLIUM_WGL "${MESA3D_OUTPUT_DIR}/x64/libgallium_wgl.dll")
        set(ZINK_DLL_X86 "${MESA3D_OUTPUT_DIR}/x86/opengl32.dll")
        set(ZINK_GALLIUM_WGL_X86 "${MESA3D_OUTPUT_DIR}/x86/libgallium_wgl.dll")
    endif()

    if(EXISTS "${ZINK_DLL}")
        set(MESA3D_AVAILABLE TRUE)
        message(STATUS "Mesa3D (core): ${ZINK_DLL} (found)")
        if(EXISTS "${ZINK_GALLIUM_WGL}")
            message(STATUS "Mesa3D Gallium: ${ZINK_GALLIUM_WGL} (found)")
        endif()
    else()
        if(NOT Python3_EXECUTABLE)
            message(FATAL_ERROR "Mesa3D fetch requires Python3, but Python3 was not found. "
                                "Install Python3 or download Mesa3D manually to third_party/mesa3d")
        endif()

        find_program(SEVEN_ZIP 7z)
        if(NOT SEVEN_ZIP)
            message(FATAL_ERROR "Mesa3D fetch requires 7z (7-Zip) command-line tool. "
                                "Install 7-Zip and ensure 7z is in PATH, "
                                "or download Mesa3D manually to third_party/mesa3d")
        endif()

        add_custom_target(omnigpu_fetch_mesa3d
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/third_party/fetch_mesa3d.py"
                    --output-dir "${MESA3D_OUTPUT_DIR}"
            COMMENT "Fetching Mesa3D (OpenGL→Vulkan) binaries..."
        )

        message(STATUS "Mesa3D: not found — will fetch automatically during build")
    endif()
endif()

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

# ---------------------------------------------------------------------------
# FFmpeg — built from submodule with HW acceleration support
# ---------------------------------------------------------------------------
if(OMNIGPU_BUILD_FFMPEG)
    if(WIN32)
        # Windows: download pre-built shared binaries (no MSYS2 required)
        set(FFMPEG_BIN_DIR "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-bin")
        set(FFMPEG_LIB_DIR "${FFMPEG_BIN_DIR}/lib")
        set(FFMPEG_INC_DIR "${FFMPEG_BIN_DIR}/include")

        if(NOT EXISTS "${FFMPEG_LIB_DIR}/avcodec.lib")
            if(NOT Python3_EXECUTABLE)
                message(FATAL_ERROR "FFmpeg download requires Python3.")
            endif()
            add_custom_target(omnigpu_fetch_ffmpeg
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_SOURCE_DIR}/third_party/fetch_ffmpeg.py"
                        --output-dir "${FFMPEG_BIN_DIR}"
                COMMENT "Downloading FFmpeg shared binaries..."
            )
            message(STATUS "FFmpeg: not found — will download automatically during build")
        else()
            message(STATUS "FFmpeg: found in third_party/ffmpeg-bin")
        endif()

        if(EXISTS "${FFMPEG_LIB_DIR}/avcodec.lib")
            set(FFMPEG_FOUND TRUE)
        endif()
    else()
        # Linux: use system FFmpeg (pkg-config)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(AVCODEC libavcodec)
            pkg_check_modules(SWSCALE libswscale)
            pkg_check_modules(AVUTIL libavutil)
        endif()
        if(AVCODEC_FOUND AND SWSCALE_FOUND AND AVUTIL_FOUND)
            set(FFMPEG_FOUND TRUE)
        endif()
    endif()

    if(FFMPEG_FOUND)
        set(OMNIGPU_USE_FFMPEG 1)
        # Copy FFmpeg DLLs to output directory for packaging
        add_custom_target(omnigpu_copy_ffmpeg_dlls ALL
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${FFMPEG_BIN_DIR}/bin/"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying FFmpeg shared DLLs to output..."
        )
        message(STATUS "FFmpeg: enabled (pre-built, HW accel via FFmpeg)")
    else()
        set(OMNIGPU_USE_FFMPEG 0)
        message(STATUS "FFmpeg: not yet downloaded — run build to fetch")
    endif()
else()
    set(OMNIGPU_USE_FFMPEG 0)
    message(STATUS "FFmpeg: disabled (set OMNIGPU_BUILD_FFMPEG=ON)")
endif()

if(OMNIGPU_BUILD_GUEST)
    # Copy full Mesa3D directory to output (when available)
    if(EXISTS "${MESA3D_OUTPUT_DIR}")
        add_custom_command(TARGET omnigpu_guest POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${MESA3D_OUTPUT_DIR}"
                "$<TARGET_FILE_DIR:omnigpu_guest>/mesa3d"
            COMMENT "Copying Mesa3D distribution..."
        )
    endif()

    # clvk DLLs
    if(DEFINED CLVK_DLL AND EXISTS "${CLVK_DLL}")
        install(FILES "${CLVK_DLL}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
endif()
