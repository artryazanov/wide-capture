#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace Graphics {
    using Microsoft::WRL::ComPtr;

    class DX11Proxy {
    public:
        DX11Proxy(ID3D11Device* device, ID3D11DeviceContext* context);
        ~DX11Proxy() = default;

        ID3D11Device* GetDevice() const { return m_device.Get(); }
        ID3D11DeviceContext* GetContext() const { return m_context.Get(); }

        // Helpers
        bool CreateTexture2D(UINT width, UINT height, DXGI_FORMAT format, UINT bindFlags, ID3D11Texture2D** ppTexture, UINT arraySize = 1, UINT miscFlags = 0);
        bool CreateCPUAccessTexture(UINT width, UINT height, DXGI_FORMAT format, ID3D11Texture2D** ppTexture);
        bool CreateStagingTexture(ID3D11Texture2D* pSource, ID3D11Texture2D** ppStaging);

    private:
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
    };
}
