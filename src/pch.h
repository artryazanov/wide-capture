#pragma once

#define WIN32_LEAN_AND_MEAN    
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

// Force SSE2 for DirectXMath
// #include <xmmintrin.h>
// #include <smmintrin.h>
#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>
#include <wrl/client.h>

#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <functional>

// Common macros
#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }
