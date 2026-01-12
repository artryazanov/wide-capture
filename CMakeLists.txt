cmake_minimum_required(VERSION 3.20)
project(WideCapture VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Compiler options for performance and safety
if(MSVC)
    add_compile_options(
        /W4 /WX-                
        /MP
        /Zc:inline              
        /D_CRT_SECURE_NO_WARNINGS
        /DNOMINMAX              
    )
endif()

# Dependencies
# D3D11 is a system library on Windows, usually no need for find_package with standard compilers
# find_package(D3D11 REQUIRED) 

# MinHook
# Configure MinHook to build as a static library
set(MINHOOK_BUILD_STATIC_LIBS ON CACHE BOOL "Build MinHook as a static library" FORCE)
set(MH_BUILD_SHARED_LIBS OFF CACHE BOOL "Build MinHook as a static library" FORCE) # Some versions use this

if(EXISTS "${CMAKE_SOURCE_DIR}/external/minhook/CMakeLists.txt")
    add_subdirectory(external/minhook)
    # Most MinHook CMake setups define a 'minhook' target
else()
    message(WARNING "MinHook not found in external/minhook. Please ensure it is present.")
    add_library(minhook INTERFACE) 
endif()

# FFmpeg
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/external/ffmpeg")
include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

set(SOURCES
    src/main.cpp
    src/pch.cpp
    src/Core/Hooks.cpp
    src/Graphics/DX11Proxy.cpp
    src/Graphics/CubemapManager.cpp
    src/Graphics/StateBlock.cpp
    src/Compute/ShaderCompiler.cpp
    src/Camera/CameraController.cpp
    src/Video/FFmpegBackend.cpp
)

set(HEADERS
    src/pch.h
    src/Core/Hooks.h
    src/Core/Logger.h
    src/Graphics/DX11Proxy.h
    src/Graphics/CubemapManager.h
    src/Graphics/StateBlock.h
    src/Compute/ShaderCompiler.h
    src/Camera/CameraController.h
    src/Video/FFmpegBackend.h
    src/Video/Encoder.h
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    minhook
    
    # FFmpeg Libraries (Precise filenames for System233 static build)
    ${FFMPEG_ROOT}/lib/libavformat.a
    ${FFMPEG_ROOT}/lib/libavcodec.a
    ${FFMPEG_ROOT}/lib/libavutil.a
    ${FFMPEG_ROOT}/lib/libswscale.a
    
    # FFmpeg Static Dependencies (Found in the lib folder)
    ${FFMPEG_ROOT}/lib/libswresample.a
    ${FFMPEG_ROOT}/lib/zlib.lib
    ${FFMPEG_ROOT}/lib/libx264.lib
    ${FFMPEG_ROOT}/lib/x265-static.lib
    ${FFMPEG_ROOT}/lib/vpxmt.lib
    ${FFMPEG_ROOT}/lib/libwebp.lib
    ${FFMPEG_ROOT}/lib/libwebpmux.lib
    ${FFMPEG_ROOT}/lib/libsharpyuv.lib
    ${FFMPEG_ROOT}/lib/jxl.lib
    ${FFMPEG_ROOT}/lib/jxl_cms.lib
    ${FFMPEG_ROOT}/lib/jxl_threads.lib
    ${FFMPEG_ROOT}/lib/hwy.lib
    ${FFMPEG_ROOT}/lib/brotlidec.lib
    ${FFMPEG_ROOT}/lib/brotlienc.lib
    ${FFMPEG_ROOT}/lib/brotlicommon.lib
    # Add others if linker errors occur (e.g. libx265, libvpx)
    
    # Windows System Libraries (Required for D3D11 and Static FFmpeg)
    d3d11
    d3dcompiler
    dxgi
    
    # Additional System Libraries often required by Static FFmpeg
    bcrypt
    secur32
    mfplat
    mfuuid
    strmiids
    ws2_32
    shlwapi
    user32
    crypt32
    ncrypt
)

target_include_directories(${PROJECT_NAME} PRIVATE 
    src
    external/DirectXMath/include
)

# Optimization flags for specific configurations
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/O2 /Oi /Ot>
    $<$<CONFIG:RelWithDebInfo>:/O2 /Oi /Ot>
)

# Copy HLSL shaders to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/src/Compute"
    "${CMAKE_BINARY_DIR}/shaders"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/src/Graphics/RGBToNV12.hlsl"
    "${CMAKE_BINARY_DIR}/RGBToNV12.hlsl"
)

message(STATUS "OmniCapture configuration complete. Build type: ${CMAKE_BUILD_TYPE}")
