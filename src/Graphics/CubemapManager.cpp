#include "pch.h"
#include "CubemapManager.h"
#include "../Compute/ShaderCompiler.h"
#include "../Video/FFmpegBackend.h"
#include "../Camera/CameraController.h"
#include "StateBlock.h"
#include "../Core/Logger.h"
#include "DX11Proxy.h"
#include <algorithm>

namespace Graphics {
    // Globals or injected dependencies
    // Ideally these should be members or singletons, but for simplicity here we assume they are managed elsewhere or attached to this.
    // However, CubemapManager needs to OWN the CameraController and Encoder per spec design.
    // Hooks.cpp has the instance of CubemapManager.
    
    // Forward declare shared instances if needed or create them here.
    
    struct CubemapManagerImpl {
        std::unique_ptr<Video::FFmpegBackend> encoder;
        std::unique_ptr<Camera::CameraController> cameraController;
        
        ComPtr<ID3D11Texture2D> faceTextures[6]; // Staging or default? Default to copy from backbuffer.
        ComPtr<ID3D11ShaderResourceView> faceSRVs[6];
        ComPtr<ID3D11Texture2D> cubeTexture; // TextureCube for compute input
        ComPtr<ID3D11ShaderResourceView> cubeSRV;
        
        ComPtr<ID3D11Texture2D> equirectTexture;
        ComPtr<ID3D11UnorderedAccessView> equirectUAV;
        ComPtr<ID3D11ShaderResourceView> equirectSRV; // Source for conversion

        // NV12 Conversion
        ComPtr<ID3D11Texture2D> equirectNV12;
        ComPtr<ID3D11RenderTargetView> nv12Y_RTV;
        ComPtr<ID3D11RenderTargetView> nv12UV_RTV;
        
        ComPtr<ID3D11VertexShader> convertVS;
        ComPtr<ID3D11PixelShader> convertPS_Y;
        ComPtr<ID3D11PixelShader> convertPS_UV;
        ComPtr<ID3D11SamplerState> linearSampler;

        ComPtr<ID3D11ComputeShader> projectionShader;
        
        UINT gameWidth = 0;
        UINT gameHeight = 0;
        UINT faceSize = 0; // Square face size
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        int currentFace = 0;
        bool isInitialized = false;
    };

    CubemapManager::CubemapManager(ID3D11Device* device, ID3D11DeviceContext* context) 
        : m_device(device), m_context(context), m_isRecording(true) { 
        // Note: m_isRecording defaults to true for auto-start or controlled via keys
        
        m_impl = std::make_unique<CubemapManagerImpl>();
        m_impl->encoder = std::make_unique<Video::FFmpegBackend>();
        m_impl->cameraController = std::make_unique<Camera::CameraController>(context);
    }

    CubemapManager::~CubemapManager() {
        if (m_impl && m_impl->encoder) {
            m_impl->encoder->Finish();
        }
        // ComPtr handles Release automatically
    }

    bool CubemapManager::IsRecording() const {
        return m_isRecording;
    }
    
    // New method required to expose camera controller to Hooks
    Camera::CameraController* CubemapManager::GetCameraController() {
        return m_impl ? m_impl->cameraController.get() : nullptr;
    }

    bool InitResources(CubemapManager* self, UINT w, UINT h, DXGI_FORMAT format) {
        auto& impl = self->m_impl;
        impl->gameWidth = w;
        impl->gameHeight = h;
        impl->format = format; 

        // 1. Determine Face Size (Must be SQUARE)
        // Use min dimension to crop center square
        impl->faceSize = std::min(w, h);
        
        LOG_INFO("Init Resources. Game: ", w, "x", h, " -> Face Size: ", impl->faceSize, "x", impl->faceSize);

        DX11Proxy proxy(self->m_device.Get(), self->m_context.Get());

        // 2. Create Face Textures (Square)
        for(int i=0; i<6; ++i) {
            if (!proxy.CreateTexture2D(impl->faceSize, impl->faceSize, format, D3D11_BIND_SHADER_RESOURCE, impl->faceTextures[i].GetAddressOf())) return false;
            self->m_device->CreateShaderResourceView(impl->faceTextures[i].Get(), nullptr, impl->faceSRVs[i].GetAddressOf());
        }

        // 2. Create Cube Texture Array (for Shader convenience, though we could use Texture2DArray)
        // Actually, the shader expects TextureCube. We need to copy faces into a TextureCube.
        // Or we use Texture2DArray and array views.
        // Spec shader: TextureCube<float4> g_InputCubemap
        
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = impl->faceSize;
        desc.Height = impl->faceSize;
        desc.MipLevels = 1;
        desc.ArraySize = 6;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        
        if (FAILED(self->m_device->CreateTexture2D(&desc, nullptr, impl->cubeTexture.GetAddressOf()))) return false;
        if (FAILED(self->m_device->CreateShaderResourceView(impl->cubeTexture.Get(), nullptr, impl->cubeSRV.GetAddressOf()))) return false;

        // 3. Equirectangular Output (UAV)
        // Resolution based on Face Size
        // Clamp to Max 4096 width (4K)
        UINT eqW = impl->faceSize * 4;
        if (eqW > 4096) eqW = 4096;
        
        UINT eqH = eqW / 2;
        
        // Align to 16 for video encoding safety
        eqW = (eqW + 15) & ~15;
        eqH = (eqH + 15) & ~15;

        LOG_INFO("Initializing Capture. Game: ", w, "x", h, " Output: ", eqW, "x", eqH);
        
        if (!proxy.CreateTexture2D(eqW, eqH, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, impl->equirectTexture.GetAddressOf())) return false;
        if (FAILED(self->m_device->CreateUnorderedAccessView(impl->equirectTexture.Get(), nullptr, impl->equirectUAV.GetAddressOf()))) return false;
        if (FAILED(self->m_device->CreateShaderResourceView(impl->equirectTexture.Get(), nullptr, impl->equirectSRV.GetAddressOf()))) return false;

        // 3b. NV12 Output & RTVs
        if (!proxy.CreateTexture2D(eqW, eqH, DXGI_FORMAT_NV12, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, impl->equirectNV12.GetAddressOf())) return false;
        
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        
        // Y Plane
        rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
        if (FAILED(self->m_device->CreateRenderTargetView(impl->equirectNV12.Get(), &rtvDesc, impl->nv12Y_RTV.GetAddressOf()))) return false;
        
        // UV Plane
        rtvDesc.Format = DXGI_FORMAT_R8G8_UNORM; 
        if (FAILED(self->m_device->CreateRenderTargetView(impl->equirectNV12.Get(), &rtvDesc, impl->nv12UV_RTV.GetAddressOf()))) return false;

        // Sampler
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        self->m_device->CreateSamplerState(&sampDesc, impl->linearSampler.GetAddressOf());

        // 4. Compile Shader
        // We assume shaders are in "shaders/" directory relative to DLL or current dir.
        // Since we copied them in CMake.
         if (FAILED(Compute::ShaderCompiler::CompileComputeShader(self->m_device.Get(), L"shaders/ProjectionShader.hlsl", "main", impl->projectionShader.GetAddressOf()))) {
             // Try current directory as fallback
             if (FAILED(Compute::ShaderCompiler::CompileComputeShader(self->m_device.Get(), L"ProjectionShader.hlsl", "main", impl->projectionShader.GetAddressOf()))) {
                 LOG_ERROR("Could not find ProjectionShader.hlsl");
                 return false;
             }
         }
         
         // Compile Conversion Shaders
         // Only compiled once, but simple check
         ID3DBlob* vsBlob = nullptr;
         ID3DBlob* psYBlob = nullptr;
         ID3DBlob* psUVBlob = nullptr;
         
         // Using D3DCompile directly or helper? Use Helper if generic. 
         // But Helper is for CS. We need generic VS/PS. 
         // Assuming ShaderCompiler has generic Compile methods or we use D3DCompile here.
         // Let's use D3DCompileFromFile since we included d3dcompiler.h in pch.h
         
         ComPtr<ID3DBlob> errBlob;
         DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;
         
         // VS
         if (FAILED(D3DCompileFromFile(L"src/Graphics/RGBToNV12.hlsl", nullptr, nullptr, "VS", "vs_5_0", flags, 0, &vsBlob, &errBlob))) {
             if (FAILED(D3DCompileFromFile(L"RGBToNV12.hlsl", nullptr, nullptr, "VS", "vs_5_0", flags, 0, &vsBlob, &errBlob))) {
                LOG_ERROR("Failed to compile RGBToNV12 VS");
                return false;
             }
         }
         self->m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, impl->convertVS.GetAddressOf());
         vsBlob->Release();

         // PS Y
         if (FAILED(D3DCompileFromFile(L"src/Graphics/RGBToNV12.hlsl", nullptr, nullptr, "PS_Y", "ps_5_0", flags, 0, &psYBlob, &errBlob))) {
              if (FAILED(D3DCompileFromFile(L"RGBToNV12.hlsl", nullptr, nullptr, "PS_Y", "ps_5_0", flags, 0, &psYBlob, &errBlob))) return false;
         }
         self->m_device->CreatePixelShader(psYBlob->GetBufferPointer(), psYBlob->GetBufferSize(), nullptr, impl->convertPS_Y.GetAddressOf());
         psYBlob->Release();

         // PS UV
         if (FAILED(D3DCompileFromFile(L"src/Graphics/RGBToNV12.hlsl", nullptr, nullptr, "PS_UV", "ps_5_0", flags, 0, &psUVBlob, &errBlob))) {
              if (FAILED(D3DCompileFromFile(L"RGBToNV12.hlsl", nullptr, nullptr, "PS_UV", "ps_5_0", flags, 0, &psUVBlob, &errBlob))) return false;
         }
         self->m_device->CreatePixelShader(psUVBlob->GetBufferPointer(), psUVBlob->GetBufferSize(), nullptr, impl->convertPS_UV.GetAddressOf());
         psUVBlob->Release();

        // 5. Init Encoder
        // 60 FPS target? Game FPS / 6.
        // If Game is 60fps, Video is 10fps. Bad.
        // Assuming we want 60fps video -> Game must range 360fps.
        // Or we record at whatever rate we get.
        if (!impl->encoder->Initialize(self->m_device.Get(), eqW, eqH, 60, "record_360.mp4")) return false;

        impl->isInitialized = true;
        return true;
    }

    void CubemapManager::ExecuteCaptureCycle(IDXGISwapChain* swapChain) {
        if (!m_impl) return;

        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf()))) return;

        D3D11_TEXTURE2D_DESC desc;
        backBuffer->GetDesc(&desc);

        if (!m_impl->isInitialized) {
            if (!InitResources(this, desc.Width, desc.Height, desc.Format)) {
                m_isRecording = false; // Stop if init fails
                return;
            }
        }
        
        // Handle Resize or Format Change (simple check based on width/height for now, ideally check format too)
        // Handle Resize
        if (desc.Width != m_impl->gameWidth || desc.Height != m_impl->gameHeight) {
            OnResize();
            return;
        }

        // 1. Copy BackBuffer to Current Face (CENTER CROP)
        // Copy regions faceSize x faceSize from center
        D3D11_BOX srcBox;
        srcBox.left = (desc.Width - m_impl->faceSize) / 2;
        srcBox.top = (desc.Height - m_impl->faceSize) / 2;
        srcBox.front = 0;
        srcBox.right = srcBox.left + m_impl->faceSize;
        srcBox.bottom = srcBox.top + m_impl->faceSize;
        srcBox.back = 1;

        m_context->CopySubresourceRegion(m_impl->faceTextures[m_impl->currentFace].Get(), 0, 0, 0, 0, backBuffer.Get(), 0, &srcBox);

        // 2. Prepare for NEXT frame camera
        m_impl->currentFace++;
        if (m_impl->currentFace >= 6) {
            // Cycle complete. Process.
            m_impl->currentFace = 0;
            
            // A. Copy Faces to Cube Texture
            for(int i=0; i<6; ++i) {
                // Must CopySubresourceRegion to specific array slice
                m_context->CopySubresourceRegion(m_impl->cubeTexture.Get(), i, 0, 0, 0, m_impl->faceTextures[i].Get(), 0, nullptr);
            }

            // B. Dispatch Compute
            StateBlock stateBlock(m_context.Get()); // Save state
            
            m_context->CSSetShader(m_impl->projectionShader.Get(), nullptr, 0);
            ID3D11ShaderResourceView* srvs[] = { m_impl->cubeSRV.Get() };
            m_context->CSSetShaderResources(0, 1, srvs);
            ID3D11UnorderedAccessView* uavs[] = { m_impl->equirectUAV.Get() };
            m_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            
            D3D11_TEXTURE2D_DESC eqDesc;
            m_impl->equirectTexture->GetDesc(&eqDesc);
            
            UINT x = (eqDesc.Width + 15) / 16;
            UINT y = (eqDesc.Height + 15) / 16;
            m_context->Dispatch(x, y, 1);
            
            // Clean up CS slots (important!)
            ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
            m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
            ID3D11ShaderResourceView* nullSRV[] = { nullptr };
            m_context->CSSetShaderResources(0, 1, nullSRV);
            
            // stateBlock destructor restores state automatically

            // C. Convert to NV12
            // Save state (already saved in stateBlock)
            // Setup for Drawing
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->IASetInputLayout(nullptr); // Bufferless
            m_context->VSSetShader(m_impl->convertVS.Get(), nullptr, 0);
            
            ID3D11ShaderResourceView* srvsConv[] = { m_impl->equirectSRV.Get() };
            m_context->PSSetShaderResources(0, 1, srvsConv);
            m_context->PSSetSamplers(0, 1, m_impl->linearSampler.GetAddressOf());
            
            // Pass 1: Y (Full Res)
            D3D11_VIEWPORT vp = {};
            vp.Width = (float)eqDesc.Width; // equirectTexture dimensions
            vp.Height = (float)eqDesc.Height;
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            m_context->RSSetViewports(1, &vp);
            
            ID3D11RenderTargetView* rtvY[] = { m_impl->nv12Y_RTV.Get() };
            m_context->OMSetRenderTargets(1, rtvY, nullptr);
            m_context->PSSetShader(m_impl->convertPS_Y.Get(), nullptr, 0);
            m_context->Draw(3, 0);
            
            // Pass 2: UV (Half Res)
            vp.Width = (float)eqDesc.Width / 2.0f;
            vp.Height = (float)eqDesc.Height / 2.0f;
            m_context->RSSetViewports(1, &vp);
            
            ID3D11RenderTargetView* rtvUV[] = { m_impl->nv12UV_RTV.Get() };
            m_context->OMSetRenderTargets(1, rtvUV, nullptr);
            m_context->PSSetShader(m_impl->convertPS_UV.Get(), nullptr, 0);
            m_context->Draw(3, 0); // Need to re-issue draw? Yes.
            
            // Clean Render Targets
            ID3D11RenderTargetView* nullRTV[] = { nullptr };
            m_context->OMSetRenderTargets(1, nullRTV, nullptr);
            ID3D11ShaderResourceView* nullSRV2[] = { nullptr };
            m_context->PSSetShaderResources(0, 1, nullSRV2);
            
            // D. Encode
            // Pass the NV12 texture
            m_impl->encoder->EncodeFrame(m_impl->equirectNV12.Get());
        }

        // Set Camera for the *upcoming* frame
        if (m_impl->cameraController) {
             m_impl->cameraController->SetTargetFace(static_cast<Camera::CubeFace>(m_impl->currentFace));
        }
    }

    void CubemapManager::OnResize() {
        if (m_impl) {
            m_impl->isInitialized = false;
            m_impl->currentFace = 0;
            // Resources will be recreated in next ExecuteCaptureCycle
        }
    }
}
