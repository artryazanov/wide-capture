cmake_minimum_required(VERSION 3.20)
project(WideCapture VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler options for performance and safety
if(MSVC)
    add_compile_options(
        /W4 /WX-                
        /MP                     
        /O2 /Oi /Ot             
        /Zc:inline              
        /D_CRT_SECURE_NO_WARNINGS
        /DNOMINMAX              
    )
endif()

# Dependencies
# D3D11 is a system library on Windows, usually no need for find_package with standard compilers
# find_package(D3D11 REQUIRED) 

# MinHook
# In a real scenario, we might use FetchContent or find_package if installed.
# Here we assume it's in external/minhook and has a CMakeLists.txt or we add it manually.
# For simplicity based on spec, assuming standard subdirectory:
if(EXISTS "${CMAKE_SOURCE_DIR}/external/minhook/CMakeLists.txt")
    add_subdirectory(external/minhook)
else()
    message(WARNING "MinHook not found in external/minhook. Please ensure it is present.")
    # Fallback or dummy target to allow configuration to proceed for code generation
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

# target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    minhook
    d3d11
    d3dcompiler
    dxgi
    avcodec
    avformat
    avutil
    swscale
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

# Copy HLSL shaders to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/src/Compute"
    "${CMAKE_BINARY_DIR}/shaders"
)

message(STATUS "OmniCapture configuration complete. Build type: ${CMAKE_BUILD_TYPE}")
