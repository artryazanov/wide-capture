#pragma once
#include <d3d11.h>
#include <dxgi.h>

namespace Core {
    // Function pointer types
    typedef HRESULT(__stdcall* DXGISwapChain_Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    typedef HRESULT(__stdcall* DXGISwapChain_ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    
    // ID3D11DeviceContext::VSSetConstantBuffers
    typedef void(__stdcall* ID3D11DeviceContext_VSSetConstantBuffers_t)(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);

    class Hooks {
    public:
        Hooks();
        ~Hooks();

        bool Install();
        void Uninstall();

    private:
        static HRESULT __stdcall Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static HRESULT __stdcall Hook_ResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
        static void __stdcall Hook_VSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);

        bool GetDX11VTable(void** pTable, size_t size);
        bool GetContextVTable(void** pTable, size_t size, ID3D11Device* device, ID3D11DeviceContext* context);

        static DXGISwapChain_Present_t oPresent;
        static DXGISwapChain_ResizeBuffers_t oResizeBuffers;
        static ID3D11DeviceContext_VSSetConstantBuffers_t oVSSetConstantBuffers;
        
        static bool m_isInitialized;
    };
}
