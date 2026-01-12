#include "pch.h"
#include "Hooks.h"
#include <atomic>

extern std::atomic<bool> g_IsRunning;
#include "MinHook.h" 
#include "../Graphics/CubemapManager.h"
#include "../Camera/CameraController.h"
#include "Logger.h"

namespace Core {
    Hooks::tPresent Hooks::oPresent = nullptr;
    Hooks::tResizeBuffers Hooks::oResizeBuffers = nullptr;
    Hooks::tSetConstantBuffers Hooks::oVSSetConstantBuffers = nullptr;

    Hooks::tSetConstantBuffers Hooks::oHSSetConstantBuffers = nullptr;
    Hooks::tSetConstantBuffers Hooks::oDSSetConstantBuffers = nullptr;
    Hooks::tSetConstantBuffers Hooks::oGSSetConstantBuffers = nullptr;
    Hooks::tSetConstantBuffers Hooks::oPSSetConstantBuffers = nullptr;

    Hooks::tSetConstantBuffers Hooks::oCSSetConstantBuffers = nullptr;
    Hooks::tDrawIndexed Hooks::oDrawIndexed = nullptr;
    Hooks::tDraw Hooks::oDraw = nullptr;
    Hooks::tMap Hooks::oMap = nullptr;
    Hooks::tUnmap Hooks::oUnmap = nullptr;
    Hooks::tUpdateSubresource Hooks::oUpdateSubresource = nullptr;
    
    bool Hooks::m_isInitialized = false;
    std::mutex Hooks::m_mutex;

    static std::unique_ptr<Graphics::CubemapManager> g_CubemapManager;

    Hooks::Hooks() {}
    Hooks::~Hooks() { Uninstall(); }

    bool Hooks::GetDX11VTable(void** pTable, size_t size) {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "DX11Temp", NULL };
        RegisterClassEx(&wc);
        HWND hWnd = CreateWindow("DX11Temp", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        
        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount = 1;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hWnd;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        IDXGISwapChain* pSwapChain = nullptr;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevels, 1, 
                                                 D3D11_SDK_VERSION, &scd, &pSwapChain, &pDevice, &featureLevel, &pContext);
        
        if (FAILED(hr)) {
            DestroyWindow(hWnd);
            return false;
        }

        // SWAP CHAIN VTABLE
        void** vTable = *reinterpret_cast<void***>(pSwapChain);
        memcpy(pTable, vTable, size);
        
        pSwapChain->Release();
        pDevice->Release();
        pContext->Release();
        DestroyWindow(hWnd);
        return true;
    }

    bool Hooks::Install() {
        if (MH_Initialize() != MH_OK) return false;

        // 1. Get SwapChain VTable
        void* pSwapChainVTable[18];
        if (!GetDX11VTable(pSwapChainVTable, sizeof(pSwapChainVTable))) return false;

        // Hook Present (8) and ResizeBuffers (13)
        if (MH_CreateHook(pSwapChainVTable[8], reinterpret_cast<LPVOID>(&Hook_Present), reinterpret_cast<void**>(&oPresent)) != MH_OK) return false;
        if (MH_CreateHook(pSwapChainVTable[13], reinterpret_cast<LPVOID>(&Hook_ResizeBuffers), reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK) return false;

        // 2. Get DeviceContext VTable
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "DX11Temp2", NULL };
        RegisterClassEx(&wc);
        HWND hWnd = CreateWindow("DX11Temp2", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
        
        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount = 1;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hWnd;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        Microsoft::WRL::ComPtr<ID3D11Device> pDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> pContext;
        Microsoft::WRL::ComPtr<IDXGISwapChain> pSwapChain;
        
        HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, pSwapChain.GetAddressOf(), pDevice.GetAddressOf(), nullptr, pContext.GetAddressOf());
        
        if (FAILED(hr) || !pContext) {
            LOG_ERROR("Failed to create temp DX11 device for Context VTable");
            DestroyWindow(hWnd);
            return false;
        }
        
        void** pContextVTable = *reinterpret_cast<void***>(pContext.Get());
        
        int iVS = 7;
        
        if (MH_CreateHook(pContextVTable[iVS], reinterpret_cast<LPVOID>(&Hook_VSSetConstantBuffers), reinterpret_cast<void**>(&oVSSetConstantBuffers)) != MH_OK) {
             LOG_ERROR("Failed to hook VS");
        }
        
        if (MH_CreateHook(pContextVTable[62], reinterpret_cast<LPVOID>(&Hook_HSSetConstantBuffers), reinterpret_cast<void**>(&oHSSetConstantBuffers)) != MH_OK) {}
        if (MH_CreateHook(pContextVTable[66], reinterpret_cast<LPVOID>(&Hook_DSSetConstantBuffers), reinterpret_cast<void**>(&oDSSetConstantBuffers)) != MH_OK) {}
        
        if (MH_CreateHook(pContextVTable[21], reinterpret_cast<LPVOID>(&Hook_GSSetConstantBuffers), reinterpret_cast<void**>(&oGSSetConstantBuffers)) != MH_OK) {
              MH_CreateHook(pContextVTable[22], reinterpret_cast<LPVOID>(&Hook_GSSetConstantBuffers), reinterpret_cast<void**>(&oGSSetConstantBuffers));
        }
        
        if (MH_CreateHook(pContextVTable[71], reinterpret_cast<LPVOID>(&Hook_CSSetConstantBuffers), reinterpret_cast<void**>(&oCSSetConstantBuffers)) != MH_OK) {
              MH_CreateHook(pContextVTable[69], reinterpret_cast<LPVOID>(&Hook_CSSetConstantBuffers), reinterpret_cast<void**>(&oCSSetConstantBuffers));
        }

        if (MH_CreateHook(pContextVTable[14], reinterpret_cast<LPVOID>(&Hook_Map), reinterpret_cast<void**>(&oMap)) != MH_OK) {
            LOG_ERROR("Failed to hook Map (14)");
        }
        
        // UpdateSubresource - disabling to be safe.
        // if (MH_CreateHook(pContextVTable[47], reinterpret_cast<LPVOID>(&Hook_UpdateSubresource), reinterpret_cast<void**>(&oUpdateSubresource)) != MH_OK) {}

    DestroyWindow(hWnd);

    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
    }

    void Hooks::Uninstall() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isInitialized) {
            g_CubemapManager.reset();
            m_isInitialized = false;
        }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }

    HRESULT __stdcall Hooks::Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (!g_IsRunning) return oPresent(pSwapChain, SyncInterval, Flags);

        std::lock_guard<std::mutex> lock(m_mutex);
        try {
            if (!m_isInitialized) {
                Microsoft::WRL::ComPtr<ID3D11Device> device;
                Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
                
                if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)device.GetAddressOf()))) {
                    device->GetImmediateContext(context.GetAddressOf());
                    
                    g_CubemapManager = std::make_unique<Graphics::CubemapManager>(device.Get(), context.Get());
                    
                    m_isInitialized = true;
                    LOG_INFO("DirectX 11 Context Initialized.");
                }
            }

            if (g_CubemapManager && g_CubemapManager->IsRecording()) {
                // If PresentHook returns false, we suppress the Present call
                if (!g_CubemapManager->PresentHook(pSwapChain)) {
                     // We return S_OK to simulate success to the game
                     return S_OK;
                }
            }
        } catch (const std::exception& e) {
             LOG_ERROR("Exception in Hook_Present: ", e.what());
        } catch (...) {
             LOG_ERROR("Unknown Exception in Hook_Present");
        }

        return oPresent(pSwapChain, SyncInterval, Flags);
    }
    
    HRESULT __stdcall Hooks::Hook_ResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        if (g_CubemapManager) {
            g_CubemapManager->OnResize();
        }
        return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    // Common Handler
    void CommonSetConstantBuffers(Hooks::tSetConstantBuffers originalFunc, const char* /*stageName*/, ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        if (!g_IsRunning) {
             if (originalFunc) originalFunc(pContext, StartSlot, NumBuffers, ppConstantBuffers);
             return;
        }

        try {
            // Diagnostic logging (Sampled)
            static int callCounter = 0;
            callCounter++;
            
            static int log_limit = 0;
            // Only log randomly to avoid spam
            if (log_limit < 10 && (callCounter % 500 == 0)) { 
                log_limit++;
            }

            ID3D11Buffer* newBuffers[14];
            bool modified = false;

            if (g_CubemapManager && g_CubemapManager->IsRecording()) {
                Camera::CameraController* camCtrl = g_CubemapManager->GetCameraController();
                if (camCtrl) {
                    UINT count = NumBuffers;
                    if (count > 14) count = 14; 
                    for(UINT i=0; i<count; ++i) {
                        newBuffers[i] = ppConstantBuffers[i];
                    }

                    for (UINT i = 0; i < count; ++i) {
                        if (newBuffers[i]) {
                             ID3D11Buffer* replacementBuf = camCtrl->CheckAndGetReplacementBuffer(pContext, newBuffers[i]);
                             if (replacementBuf) {
                                 newBuffers[i] = replacementBuf;
                                 modified = true;
                             }
                        }
                    }
                }
            }

            if (originalFunc) {
                originalFunc(pContext, StartSlot, NumBuffers, modified ? newBuffers : ppConstantBuffers);
            }
        } catch (...) {
            if (originalFunc) originalFunc(pContext, StartSlot, NumBuffers, ppConstantBuffers);
        }
    }

    void __stdcall Hooks::Hook_VSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        CommonSetConstantBuffers(oVSSetConstantBuffers, "VS", pContext, StartSlot, NumBuffers, ppConstantBuffers);
    }
    void __stdcall Hooks::Hook_HSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        CommonSetConstantBuffers(oHSSetConstantBuffers, "HS", pContext, StartSlot, NumBuffers, ppConstantBuffers);
    }
    void __stdcall Hooks::Hook_DSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        CommonSetConstantBuffers(oDSSetConstantBuffers, "DS", pContext, StartSlot, NumBuffers, ppConstantBuffers);
    }
    void __stdcall Hooks::Hook_GSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        CommonSetConstantBuffers(oGSSetConstantBuffers, "GS", pContext, StartSlot, NumBuffers, ppConstantBuffers);
    }
    void __stdcall Hooks::Hook_PSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        CommonSetConstantBuffers(oPSSetConstantBuffers, "PS", pContext, StartSlot, NumBuffers, ppConstantBuffers);
    }
    void __stdcall Hooks::Hook_CSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        CommonSetConstantBuffers(oCSSetConstantBuffers, "CS", pContext, StartSlot, NumBuffers, ppConstantBuffers);
    }

HRESULT __stdcall Hooks::Hook_Map(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource) {
    try {
        static int mapHitCount = 0;
        if (mapHitCount < 5) { LOG_INFO("Hook_Map HIT"); mapHitCount++; }

        HRESULT hr = oMap(pContext, pResource, Subresource, MapType, MapFlags, pMappedResource);
        if (SUCCEEDED(hr) && pMappedResource && g_CubemapManager) {
            if (MapType == D3D11_MAP_WRITE_DISCARD || MapType == D3D11_MAP_WRITE || MapType == D3D11_MAP_WRITE_NO_OVERWRITE) {
                 Camera::CameraController* camCtrl = g_CubemapManager->GetCameraController();
                 if (camCtrl) camCtrl->OnMap(pResource, pMappedResource); 
            }
        }
        return hr;
    } catch (...) {
        return oMap(pContext, pResource, Subresource, MapType, MapFlags, pMappedResource); 
    }
}

    void __stdcall Hooks::Hook_Unmap(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource) {
        // Just call original safely
        oUnmap(pContext, pResource, Subresource);
    }

    void __stdcall Hooks::Hook_UpdateSubresource(ID3D11DeviceContext* pContext, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) {
        try {
            if (g_CubemapManager) {
                Camera::CameraController* camCtrl = g_CubemapManager->GetCameraController();
                if (camCtrl) camCtrl->OnUpdateSubresource(pDstResource, pSrcData, pDstBox); 
            }
        } catch (...) {}
        oUpdateSubresource(pContext, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    }

}
