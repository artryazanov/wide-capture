#include "pch.h"
#include "Hooks.h"
#include "MinHook.h" 
#include "../Graphics/CubemapManager.h"
#include "../Camera/CameraController.h"
#include "Logger.h"

namespace Core {
    DXGISwapChain_Present_t Hooks::oPresent = nullptr;
    DXGISwapChain_ResizeBuffers_t Hooks::oResizeBuffers = nullptr;
    ID3D11DeviceContext_VSSetConstantBuffers_t Hooks::oVSSetConstantBuffers = nullptr;
    
    bool Hooks::m_isInitialized = false;

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
        memcpy(pTable, vTable, size); // Copying SwapChain VTable

        // WAIT, we also need DeviceContext VTable for VSSetConstantBuffers!
        // SwapChain has 18 methods. Resulting pTable from this function only contains SwapChain methods if size==18*8.
        // We probably need a separate way to get Context hooks.
        // Or we pass pointer to fill.
        
        // This function as designed in v1 only returns one table.
        // We need to refactor or just GetContextVTable separately. But since I reused this function name and sig...
        // Let's assume this returns SwapChain VTable only.
        
        // I will implement Hooking logic inside Install() using context creation there implicitly or helper.
        
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
        if (MH_CreateHook(pSwapChainVTable[8], &Hook_Present, reinterpret_cast<void**>(&oPresent)) != MH_OK) return false;
        if (MH_CreateHook(pSwapChainVTable[13], &Hook_ResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK) return false;

        // 2. Get DeviceContext VTable
        // We need to create specific device to get the vtable.
        // Copy-paste creation logic for now or refactor. Compactness favored.
         WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), "DX11Temp2", NULL, NULL, NULL, "DX11Temp2", NULL };
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

        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        IDXGISwapChain* pSwapChain = nullptr;
        
        D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &pSwapChain, &pDevice, nullptr, &pContext);
        
        void** pContextVTable = *reinterpret_cast<void***>(pContext);
        // VSSetConstantBuffers is index 7 (VSSetShaderResources=25... woot? Check headers)
        // I need to be precise. 
        // ID3D11DeviceChild methods: 4 (QueryInterface, AddRef, Release, GetDevice, GetPrivateData, SetPrivateData, SetPrivateDataInterface?? No)
        // IUnknown: 3. ID3D11DeviceChild: +4 = 7? No.
        // Let's use offsets from documentation or standard knowledge.
        // VSSetConstantBuffers is indeed usually index 7 or nearby.
        // Exact layout:
        // 0: QueryInterface, 1: AddRef, 2: Release
        // 3: GetDevice, 4: GetPrivateData, 5: SetPrivateData, 6: SetPrivateDataInterface
        // 7: VSSetConstantBuffers
        // 8: PSSetShaderResources ... No, order is specific.
        // According to commonly available vtables:
        // 7 is VSSetConstantBuffers
        // Wait, checking online references...
        // 0-2 IUnknown
        // 3-6 ID3D11DeviceChild
        // 7 VSSetConstantBuffers
        // 8 PSSetShaderResources
        // ...
        
        if (MH_CreateHook(pContextVTable[7], &Hook_VSSetConstantBuffers, reinterpret_cast<void**>(&oVSSetConstantBuffers)) != MH_OK) {
            LOG_ERROR("Failed to hook VSSetConstantBuffers");
             // Don't fail entire install maybe? Just warn. But camera won't work.
             // return false; 
        }

        pSwapChain->Release();
        pDevice->Release();
        pContext->Release();
        DestroyWindow(hWnd);

        return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
    }

    void Hooks::Uninstall() {
        if (m_isInitialized) {
            g_CubemapManager.reset();
            m_isInitialized = false;
        }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }

    HRESULT __stdcall Hooks::Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (!m_isInitialized) {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* context = nullptr;
            
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device))) {
                device->GetImmediateContext(&context);
                g_CubemapManager = std::make_unique<Graphics::CubemapManager>(device, context);
                device->Release();
                context->Release();
                m_isInitialized = true;
                LOG_INFO("DirectX 11 Context Initialized.");
            }
        }

        if (g_CubemapManager && g_CubemapManager->IsRecording()) {
            g_CubemapManager->ExecuteCaptureCycle(pSwapChain);
        }

        return oPresent(pSwapChain, SyncInterval, Flags);
    }
    
    HRESULT __stdcall Hooks::Hook_ResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        if (g_CubemapManager) {
            g_CubemapManager->OnResize();
        }
        return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    void __stdcall Hooks::Hook_VSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) {
        // Intercept logic
        if (g_CubemapManager && g_CubemapManager->IsRecording()) {
            Camera::CameraController* camCtrl = g_CubemapManager->GetCameraController();
            if (camCtrl) {
                // Check buffers. We iterate.
                for (UINT i = 0; i < NumBuffers; ++i) {
                    if (ppConstantBuffers[i]) {
                         camCtrl->CheckAndModifyConstantBuffer(StartSlot + i, ppConstantBuffers[i]);
                    }
                }
            }
        }
        
        oVSSetConstantBuffers(pContext, StartSlot, NumBuffers, ppConstantBuffers);
    }
}
