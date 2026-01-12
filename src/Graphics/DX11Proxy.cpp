#include "pch.h"
#include "DX11Proxy.h"
#include "../Core/Logger.h"

namespace Graphics {
    DX11Proxy::DX11Proxy(ID3D11Device* device, ID3D11DeviceContext* context) 
        : m_device(device), m_context(context) {}

    bool DX11Proxy::CreateTexture2D(UINT width, UINT height, DXGI_FORMAT format, UINT bindFlags, ID3D11Texture2D** ppTexture, UINT arraySize, UINT miscFlags) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = arraySize;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = bindFlags;
        desc.MiscFlags = miscFlags;
        
        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, ppTexture);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create Texture2D. HR: ", std::hex, hr);
            return false;
        }
        return true;
    }

    bool DX11Proxy::CreateCPUAccessTexture(UINT width, UINT height, DXGI_FORMAT format, ID3D11Texture2D** ppTexture) {
         D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        
        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, ppTexture);
         if (FAILED(hr)) {
            LOG_ERROR("Failed to create CPU Texture2D. HR: ", std::hex, hr);
            return false;
        }
        return true;
    }

    bool DX11Proxy::CreateStagingTexture(ID3D11Texture2D* pSource, ID3D11Texture2D** ppStaging) {
        D3D11_TEXTURE2D_DESC desc;
        pSource->GetDesc(&desc);

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, ppStaging);
        if (FAILED(hr)) {
             LOG_ERROR("Failed to create staging texture. HR: ", std::hex, hr);
            return false;
        }
        return true;
    }
}
