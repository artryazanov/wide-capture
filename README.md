# WideCapture

WideCapture is a 360-degree video capture tool for DirectX 11 games, now powered by the ReShade Add-on API.

## Features

- **Single-Frame Capture**: Captures all 6 faces of a cubemap within a single game frame, eliminating motion artifacts caused by camera rotation.
- **Auto-Detection**: Automatically detects game camera matrices (View/Projection) using heuristic scanning of Constant Buffers.
- **Hardware Encoding**: Uses NVENC/AMF via FFmpeg for high-performance recording.
- **ReShade Add-on**: Integrated as a ReShade Add-on for better compatibility and stability.

## Requirements

- **ReShade 5.0+** with Add-on support enabled.
- **Windows 10/11**.
- **DirectX 11** game.
- **NVIDIA/AMD GPU** with hardware encoding support.

## Installation

1. Install ReShade with full Add-on support to your target game.
2. Place `WideCapture.addon` (rename the built DLL) into the game folder (where ReShade is installed).
3. Ensure `ffmpeg` libraries are available or statically linked.

## Usage

- The addon automatically activates when the game starts.
- It scans for the camera buffer. Once found, it begins recording 360 video to `widecapture_reshade.mp4`.
- **Note**: This is an experimental build. Performance impact is significant due to multi-view rendering (6x geometry pass).

## Building

1. Ensure you have CMake and Visual Studio installed.
2. The project fetches ReShade headers automatically or expects them in `external/reshade/include`.
3. Run CMake configuration and build.

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Architecture

- **Core**: ReShade Event hooks (`main.cpp`).
- **Camera**: Matrix detection and manipulation (`CameraController`).
- **Graphics**: Multi-view rendering loop and Projection Compute Shader (`CubemapManager`).
- **Video**: FFmpeg NV12 encoding (`FFmpegBackend`).

## License

MIT
