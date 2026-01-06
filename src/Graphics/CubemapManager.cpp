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
        
        ComPtr<ID3D11ComputeShader> projectionShader;
        
        UINT width = 0;
        UINT height = 0;
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
        impl->width = w;
        impl->height = h;
        impl->format = format; // Store the format

        DX11Proxy proxy(self->m_device.Get(), self->m_context.Get());

        // 1. Create Face Textures
        // Use the game's backbuffer format for compatibility (e.g. SRGB or BGR)
        for(int i=0; i<6; ++i) {
            if (!proxy.CreateTexture2D(w, h, format, D3D11_BIND_SHADER_RESOURCE, impl->faceTextures[i].GetAddressOf())) return false;
            self->m_device->CreateShaderResourceView(impl->faceTextures[i].Get(), nullptr, impl->faceSRVs[i].GetAddressOf());
        }

        // 2. Create Cube Texture Array (for Shader convenience, though we could use Texture2DArray)
        // Actually, the shader expects TextureCube. We need to copy faces into a TextureCube.
        // Or we use Texture2DArray and array views.
        // Spec shader: TextureCube<float4> g_InputCubemap
        
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
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
        // Width = 4 * Height (roughly, or 2*Pi*R vs Pi*R). Usually 2:1 aspect ratio.
        // If Face is WxH, Sphere is roughly 4*W x 2*W?
        // Standard: 4096x2048 for 4K.
        // 3. Equirectangular Output (UAV)
        // Adjust resolution: Clamp to Max 4096 width (4K) to stay within safe H.264 levels
        // Typical equirectangular is 2:1 aspect ratio.
        UINT eqW = std::min<UINT>(w * 4, 4096); 
        UINT eqH = eqW / 2;
        
        // Align to 16 for video encoding safety
        eqW = (eqW + 15) & ~15;
        eqH = (eqH + 15) & ~15;

        LOG_INFO("Initializing Capture. Game: ", w, "x", h, " Output: ", eqW, "x", eqH);
        
        if (!proxy.CreateTexture2D(eqW, eqH, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, impl->equirectTexture.GetAddressOf())) return false;
        if (FAILED(self->m_device->CreateUnorderedAccessView(impl->equirectTexture.Get(), nullptr, impl->equirectUAV.GetAddressOf()))) return false;

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

        // 5. Init Encoder
        // 60 FPS target? Game FPS / 6.
        // If Game is 60fps, Video is 10fps. Bad.
        // Assuming we want 60fps video -> Game must range 360fps.
        // Or we record at whatever rate we get.
        if (!impl->encoder->Initialize(self->m_device.Get(), eqW, eqH, 60, "C:/Videos/record_360.mp4")) return false;

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
        if (desc.Width != m_impl->width || desc.Height != m_impl->height) {
            OnResize();
            return;
        }

        // 1. Copy BackBuffer to Current Face
        // Use CopySubresourceRegion for safety if formats match. 
        // If InitResources used desc.Format, they match.
        m_context->CopySubresourceRegion(m_impl->faceTextures[m_impl->currentFace].Get(), 0, 0, 0, 0, backBuffer.Get(), 0, nullptr);

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

            // C. Encode
            m_impl->encoder->EncodeFrame(m_impl->equirectTexture.Get());
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
