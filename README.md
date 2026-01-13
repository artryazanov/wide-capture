# WideCapture

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20DirectX%2011-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)

**WideCapture** is an experimental utility designed to capture 360-degree (equirectangular) gameplay video from DirectX 11 applications. It utilizes **DLL Injection**, **API Hooking**, **Compute Shaders**, and **Zero-Copy Hardware Encoding** to achieve panoramic recording without requiring access to the game's source code.

> [!WARNING]
> **Development Status: Proof of Concept**
>
> This project is currently in the **Alpha** stage. While the core infrastructure (injection, hooking, and FFmpeg integration) is implemented, advanced features like reliable camera control and seamless stitching are still in development.
>
> The documentation below outlines the **technical goals and architecture**. Please anticipate instability, visual artifacts, and API limitations in the current build.

## 🚀 Key Features

*   **Universal Compatibility**: Works with most DirectX 11 games (Unity, Unreal Engine, proprietary engines).
*   **Zero-Copy Pipeline**: Data never leaves the GPU. Surfaces are shared directly between DirectX and the Video Encoder.
*   **Compute Shader Stitching**: Converts 6 cubemap faces to Equirectangular projection purely on the GPU.
*   **Hardware Acceleration**: Direct integration with FFmpeg's D3D11VA and NVENC/AMF for substantial performance.
*   **Camera Hijacking**: Heuristic analysis of Constant Buffers to identify and manipulate the game's camera for 6-axis rendering.

## 🛠️ Architecture

WideCapture functions by injecting into the target process and hooking key graphics API calls:

1.  **Injection**: The DLL enters the process and spins up a capture thread.
2.  **Hooking**: Intercepts `IDXGISwapChain::Present` to control the frame cycle and `ID3D11DeviceContext::VSSetConstantBuffers` to manipulate the camera.
3.  **Rendering**: For every single game frame, the engine is tricked into rendering 6 times (Front, Back, Left, Right, Up, Down).
4.  **Stitching**: A Compute Shader (`ProjectionShader.hlsl`) samples these 6 views and writes to a single Equirectangular texture.
5.  **Encoding**: The texture is passed to FFmpeg via D3D11 hardware context for immediate H.264 encoding.

## 📋 Prerequisites

*   **OS**: Windows 10/11 (x64)
*   **Compiler**: MSVC v142+ (Visual Studio 2019/2022) with C++17 support.
    *   **Installation Requirement**: You must install the **Desktop development with C++** workload.
        1.  Open the **Visual Studio Installer**.
        2.  Select the **Workloads** tab.
        3.  Check **Desktop development with C++**.
        4.  Ensure the "Installation details" on the right include:
            *   **MSVC ... C++ x64/x86 build tools**
            *   **Windows 10 (or 11) SDK**
*   **Tools**: CMake 3.20+, Python 3.x (for injection scripts).

### External Dependencies
Ensure the following libraries are placed in the `external/` directory:

*   **DirectXMath**: [microsoft/DirectXMath](https://github.com/microsoft/DirectXMath)
    1.  Download the source code or specific header files.
    2.  Place the header files in `external/DirectXMath/include`.
    3.  Ensure the structure looks like this:

        ```text
        external/DirectXMath/
        └── include/
            ├── DirectXMath.h
            ├── DirectXMathConvert.inl
            ├── DirectXMathMatrix.inl
            ├── DirectXMathMisc.inl
            ├── DirectXMathVector.inl
            └── ...
        ```
    This external dependency is required to ensure full DirectXMath support on MinGW, which may have incomplete system headers.


*   **FFmpeg (Static Libraries for MSVC)**:
    1.  Download a **static** build from [artryazanov/ffmpeg-msvc-prebuilt](https://github.com/artryazanov/ffmpeg-msvc-prebuilt/releases) (e.g., `ffmpeg-n8.0.1-gpl-amd64-static.zip`).
        *   *Note: Standard builds from gyan.dev do not contain the necessary static libraries for MSVC.*
    2.  Extract the contents into `external/ffmpeg`.
    3.  Ensure the structure matches:

        ```text
        external/ffmpeg/
        ├── include/          # Header files (.h)
        │   ├── libavcodec/
        │   ├── libavformat/
        │   └── ...
        └── lib/              # Static libraries (.a)
            ├── libavcodec.a
            ├── libavformat.a
            ├── ...
            ├── pkgconfig/
            └── ...
        ```

    > **Note**: This project is configured to link FFmpeg statically. You do **not** need to copy any FFmpeg DLLs to the output directory.

*   **MinHook**: [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)
    1.  Clone the repository or download the source code.
    2.  Place the contents in `external/minhook`.
    3.  Ensure `CMakeLists.txt` is located at `external/minhook/CMakeLists.txt`. Structure:

        ```text
        external/minhook/
        ├── CMakeLists.txt
        ├── include/
        │   └── MinHook.h
        ├── src/
        └── ...
        ```

    The project will automatically build MinHook as part of the solution.

## 🔨 Build Instructions

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/artryazanov/wide-capture.git
    cd WideCapture
    ```

2.  **Configure with CMake**:
    ```bash
    mkdir build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    ```

3.  **Build**:
    ```bash
    cmake --build . --config Release
    ```

    *Artifacts `WideCapture.dll` and `shaders/` will be generated in `build/Release`.*

### 🔧 IDE Configuration: CLion

If you use JetBrains CLion, critical configuration is required to match the MSVC environment:

1.  **Toolchain**:
    *   Go to `Settings` -> `Build, Execution, Deployment` -> `Toolchains`.
    *   Add a **Visual Studio** toolchain (Click `+` -> `Visual Studio`).
    *   **Do not use MinGW or Cygwin**. The static libraries are linked for MSVC.
    *   Ensure **Architecture** is set to `x64` (amd64).

2.  **Set as Default**:
    *   In the **Toolchains** list, select the **Visual Studio** toolchain you created.
    *   Use the **Up Arrow** button (or drag and drop) to **move it to the very top** of the list.
    *   This ensures CLion automatically uses MSVC as the default compiler for all CMake profiles.

## 📦 Deployment (Installation)

To use WideCapture with a target game, you must copy the built artifacts to the game's executable directory (where `Game.exe` is located).

**Required Files:**
1.  **`WideCapture.dll`**: The main library.
2.  **`RGBToNV12.hlsl`**: Shader for video conversion.
3.  **`shaders/` folder**: Contains `ProjectionShader.hlsl` (Core compute shader).

**Steps:**
1.  Go to your build output folder (e.g., `build/Release/`).
2.  Copy `WideCapture.dll` and `RGBToNV12.hlsl`.
3.  Copy the `shaders` folder.
4.  Paste them all into the folder containing the game executable.

*Folder Structure Example:*
```text
GameFolder/
├── Game.exe
├── WideCapture.dll
├── RGBToNV12.hlsl
└── shaders/
    └── ProjectionShader.hlsl
```

## 🎮 Usage

### 1. Injection
Use the provided Python script to inject the DLL into your target game.

```bash
python scripts/inject.py <process_name.exe> <path/to/WideCapture.dll>
```

*Example:* `python scripts/inject.py SkyrimSE.exe build/Release/WideCapture.dll`

### 2. Controls
*   **Automatic Start**: Recording begins immediately upon successful initialization of the graphics hooks.
*   **Unload**: Press the `END` key to stop recording, finalize the MP4 file, and unload the DLL safely.

### 3. Post-Processing (Metadata)
To allow video players (YouTube, VLC) to recognize the file as 360° video, inject the spatial media metadata:

```bash
# Requires Google's spatial-media tool
python spatial_media_injector.py -i record_360.mp4 -o final_output.mp4
```

## 📂 Project Structure

```
WideCapture/
├── src/
│   ├── Core/           # Hooks, Logger, Entry Point
│   ├── Graphics/       # DX11 Management, Cubemap Logic
│   ├── Compute/        # Shaders & Compilation
│   ├── Camera/         # Matrix Math & Camera Control
│   └── Video/          # FFmpeg D3D11VA Backend
├── external/           # Dependencies (MinHook, FFmpeg, DirectXMath)
├── scripts/            # Injection & Utility scripts
└── CMakeLists.txt      # Build Configuration
```

## ⚠️ Disclaimer

This software is for educational and research purposes only. Using this tool in multiplayer games may trigger anti-cheat software (VAC, BattlEye, EasyAntiCheat) resulting in bans. Use only in single-player modes or controlled environments.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

### Third-Party Licenses

This project makes use of the following third-party libraries:

*   **MinHook**: Developed by TsudaKageyu. Licensed under the [BSD 2-Clause License](https://github.com/TsudaKageyu/minhook/blob/master/LICENSE.txt).
*   **FFmpeg**: Licensed under the [GNU General Public License (GPL) version 3](https://www.gnu.org/licenses/gpl-3.0.html). 
    *   This software uses code of <a href=http://ffmpeg.org>FFmpeg</a> licensed under the <a href=http://www.gnu.org/licenses/gpl.html>GPLv3</a> and its source can be downloaded [here](https://ffmpeg.org/download.html).
*   **DirectXMath**: Developed by Microsoft. Licensed under the [MIT License](https://github.com/microsoft/DirectXMath/blob/main/LICENSE).