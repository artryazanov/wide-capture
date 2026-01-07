#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <mutex>

namespace Core {
    class Hooks {
    public:
        Hooks();
        ~Hooks();

        bool Install();
        void Uninstall();

        // Function pointer types
        typedef HRESULT(__stdcall* tPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        typedef HRESULT(__stdcall* tResizeBuffers)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        // Unified signature for all SetConstantBuffers
        typedef void(__stdcall* tSetConstantBuffers)(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);
        
        typedef HRESULT(__stdcall* tMap)(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource);
        typedef void(__stdcall* tUnmap)(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource);
        typedef void(__stdcall* tUpdateSubresource)(ID3D11DeviceContext* pContext, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);

        // Static Hooks
        static HRESULT __stdcall Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static HRESULT __stdcall Hook_ResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

        static void __stdcall Hook_VSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);
        static void __stdcall Hook_HSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);
        static void __stdcall Hook_DSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);
        static void __stdcall Hook_GSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);
        static void __stdcall Hook_PSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);
        static void __stdcall Hook_CSSetConstantBuffers(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers);
        
        static void __stdcall Hook_DrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
        static void __stdcall Hook_Draw(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation);

        static HRESULT __stdcall Hook_Map(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource);
        static void __stdcall Hook_Unmap(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource);
        static void __stdcall Hook_UpdateSubresource(ID3D11DeviceContext* pContext, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);

    private:
        bool GetDX11VTable(void** pTable, size_t size);

        typedef void(__stdcall* tDrawIndexed)(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
        typedef void(__stdcall* tDraw)(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation);

        static tPresent oPresent;
        static tResizeBuffers oResizeBuffers;
        static tSetConstantBuffers oVSSetConstantBuffers;
        static tSetConstantBuffers oHSSetConstantBuffers;
        static tSetConstantBuffers oDSSetConstantBuffers;
        static tSetConstantBuffers oGSSetConstantBuffers;
        static tSetConstantBuffers oPSSetConstantBuffers;
        static tSetConstantBuffers oCSSetConstantBuffers;
        
        static tDrawIndexed oDrawIndexed;
        static tDraw oDraw;
        
        static tMap oMap;
        static tUnmap oUnmap;
        static tUpdateSubresource oUpdateSubresource;
        
        static bool m_isInitialized;
        static std::mutex m_mutex;
    };
}
