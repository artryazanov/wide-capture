#include "pch.h"
#include "CubemapManager.h"
#include "../Compute/ShaderCompiler.h"
#include "../Core/Logger.h"
#include <d3dcompiler.h>
#include <algorithm>
#include "StateBlock.h"

namespace Graphics {

    CubemapManager::CubemapManager(reshade::api::device* device) : m_device(device) {
        m_cameraController = std::make_unique<Camera::CameraController>();
        m_encoder = std::make_unique<Video::FFmpegBackend>();
    }

    CubemapManager::~CubemapManager() {
        DestroyResources();
    }

    void CubemapManager::DestroyResources() {
        if (m_device) {
            for (int i = 0; i < 6; ++i) {
                if (m_faceRtvs[i].handle) m_device->destroy_resource_view(m_faceRtvs[i]);
                if (m_faceSrvs[i].handle) m_device->destroy_resource_view(m_faceSrvs[i]);
                if (m_faceTextures[i].handle) m_device->destroy_resource(m_faceTextures[i]);
            }
            if (m_cubeSrv.handle) m_device->destroy_resource_view(m_cubeSrv);
            if (m_cubeTexture.handle) m_device->destroy_resource(m_cubeTexture);
            if (m_equirectUAV.handle) m_device->destroy_resource_view(m_equirectUAV);
            if (m_equirectSRV.handle) m_device->destroy_resource_view(m_equirectSRV);
            if (m_equirectTexture.handle) m_device->destroy_resource(m_equirectTexture);
        }

        m_nv12Y_RTV.Reset();
        m_nv12UV_RTV.Reset();
        m_equirectNV12.Reset();
        m_projectionShader.Reset();
        m_convertVS.Reset();
        m_convertPS_Y.Reset();
        m_convertPS_UV.Reset();
        m_linearSampler.Reset();

        if (m_encoder) m_encoder->Finish();
    }

    bool CubemapManager::InitResources(uint32_t width, uint32_t height) {
        if (m_width == width && m_height == height && m_faceTextures[0].handle != 0) return true;
        
        DestroyResources();

        m_width = width;
        m_height = height;
        m_faceSize = std::min(width, height); // Keep it square

        // 1. Create Face Textures (R8G8B8A8 UNORM)
        for (int i = 0; i < 6; ++i) {
            if (!m_device->create_resource(
                reshade::api::resource_desc(m_faceSize, m_faceSize, 1, 1, reshade::api::format::r8g8b8a8_unorm, 1, reshade::api::memory_heap::gpu_only, reshade::api::resource_usage::render_target | reshade::api::resource_usage::copy_source | reshade::api::resource_usage::shader_resource),
                nullptr, reshade::api::resource_usage::shader_resource, &m_faceTextures[i]))
            {
                LOG_ERROR("Failed to create face texture ", i);
                return false;
            }

            if (!m_device->create_resource_view(m_faceTextures[i], reshade::api::resource_usage::render_target,
                reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_2d, reshade::api::format::r8g8b8a8_unorm, 0, 1, 0, 1), &m_faceRtvs[i]))
                return false;

            if (!m_device->create_resource_view(m_faceTextures[i], reshade::api::resource_usage::shader_resource,
                reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_2d, reshade::api::format::r8g8b8a8_unorm, 0, 1, 0, 1), &m_faceSrvs[i]))
                return false;
        }

        // 2. Create Cube Texture Array (for Compute Shader)
        if (!m_device->create_resource(
            reshade::api::resource_desc(reshade::api::resource_type::texture_2d, m_faceSize, m_faceSize, 6, 1, reshade::api::format::r8g8b8a8_unorm, 1, reshade::api::memory_heap::gpu_only, reshade::api::resource_usage::copy_dest | reshade::api::resource_usage::shader_resource),
            nullptr, reshade::api::resource_usage::shader_resource, &m_cubeTexture))
            return false;

        if (!m_device->create_resource_view(m_cubeTexture, reshade::api::resource_usage::shader_resource,
            reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_cube, reshade::api::format::r8g8b8a8_unorm, 0, 1, 0, 6), &m_cubeSrv))
            return false;

        // 3. Equirectangular Output
        UINT eqW = m_faceSize * 4;
        UINT eqH = eqW / 2;
        // Align to 16
        eqW = (eqW + 15) & ~15;
        eqH = (eqH + 15) & ~15;

        if (!m_device->create_resource(
            reshade::api::resource_desc(eqW, eqH, 1, 1, reshade::api::format::r8g8b8a8_unorm, 1, reshade::api::memory_heap::gpu_only, reshade::api::resource_usage::unordered_access | reshade::api::resource_usage::shader_resource),
            nullptr, reshade::api::resource_usage::unordered_access, &m_equirectTexture))
            return false;

        if (!m_device->create_resource_view(m_equirectTexture, reshade::api::resource_usage::unordered_access,
            reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_2d, reshade::api::format::r8g8b8a8_unorm, 0, 1, 0, 1), &m_equirectUAV))
            return false;

        if (!m_device->create_resource_view(m_equirectTexture, reshade::api::resource_usage::shader_resource,
            reshade::api::resource_view_desc(reshade::api::resource_view_type::texture_2d, reshade::api::format::r8g8b8a8_unorm, 0, 1, 0, 1), &m_equirectSRV))
            return false;

        // 4. Native D3D11 Initialization for Shaders/FFmpeg
        ID3D11Device* d3d11Dev = (ID3D11Device*)m_device->get_native();
        if (!d3d11Dev) return false;

        // Compile Projection Shader
        if (FAILED(Compute::ShaderCompiler::CompileComputeShader(d3d11Dev, L"shaders/ProjectionShader.hlsl", "main", m_projectionShader.GetAddressOf()))) {
             // Fallback try local
             if (FAILED(Compute::ShaderCompiler::CompileComputeShader(d3d11Dev, L"ProjectionShader.hlsl", "main", m_projectionShader.GetAddressOf()))) {
                 LOG_ERROR("Failed to compile ProjectionShader");
                 // Continue anyway to allow build
             }
        }

        // Compile RGB->NV12
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psYBlob = nullptr;
        ID3DBlob* psUVBlob = nullptr;
        
        if (FAILED(D3DCompileFromFile(L"RGBToNV12.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsBlob, nullptr))) {
             if (FAILED(D3DCompileFromFile(L"src/Graphics/RGBToNV12.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsBlob, nullptr)))
                LOG_ERROR("Failed RGBToNV12 VS");
        }
        if (vsBlob) {
            d3d11Dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_convertVS.GetAddressOf());
            vsBlob->Release();
        }

        if (FAILED(D3DCompileFromFile(L"RGBToNV12.hlsl", nullptr, nullptr, "PS_Y", "ps_5_0", 0, 0, &psYBlob, nullptr))) {
             if (FAILED(D3DCompileFromFile(L"src/Graphics/RGBToNV12.hlsl", nullptr, nullptr, "PS_Y", "ps_5_0", 0, 0, &psYBlob, nullptr))) {}
        }
        if (psYBlob) {
            d3d11Dev->CreatePixelShader(psYBlob->GetBufferPointer(), psYBlob->GetBufferSize(), nullptr, m_convertPS_Y.GetAddressOf());
            psYBlob->Release();
        }

        if (FAILED(D3DCompileFromFile(L"RGBToNV12.hlsl", nullptr, nullptr, "PS_UV", "ps_5_0", 0, 0, &psUVBlob, nullptr))) {
             if (FAILED(D3DCompileFromFile(L"src/Graphics/RGBToNV12.hlsl", nullptr, nullptr, "PS_UV", "ps_5_0", 0, 0, &psUVBlob, nullptr))) {}
        }
        if (psUVBlob) {
            d3d11Dev->CreatePixelShader(psUVBlob->GetBufferPointer(), psUVBlob->GetBufferSize(), nullptr, m_convertPS_UV.GetAddressOf());
            psUVBlob->Release();
        }

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        d3d11Dev->CreateSamplerState(&sampDesc, m_linearSampler.GetAddressOf());

        // Create NV12 Resources
        D3D11_TEXTURE2D_DESC nv12Desc = {};
        nv12Desc.Width = eqW;
        nv12Desc.Height = eqH;
        nv12Desc.MipLevels = 1;
        nv12Desc.ArraySize = 1;
        nv12Desc.Format = DXGI_FORMAT_NV12;
        nv12Desc.SampleDesc.Count = 1;
        nv12Desc.Usage = D3D11_USAGE_DEFAULT;
        nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(d3d11Dev->CreateTexture2D(&nv12Desc, nullptr, m_equirectNV12.GetAddressOf()))) return false;

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
        d3d11Dev->CreateRenderTargetView(m_equirectNV12.Get(), &rtvDesc, m_nv12Y_RTV.GetAddressOf());

        rtvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        d3d11Dev->CreateRenderTargetView(m_equirectNV12.Get(), &rtvDesc, m_nv12UV_RTV.GetAddressOf());

        // Init Encoder
        if (m_encoder && !m_encoder->Initialize(d3d11Dev, eqW, eqH, 60, "widecapture_reshade.mp4")) return false;

        return true;
    }

    void CubemapManager::OnUpdateBuffer(reshade::api::device* device, reshade::api::resource resource, const void* data, uint64_t size) {
        if (m_cameraController) {
            m_cameraController->OnUpdateBuffer(resource, data, size);
        }
    }

    void CubemapManager::OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline) {
        // Track pipeline if necessary
    }

    void CubemapManager::ProcessDraw(reshade::api::command_list* cmd_list, bool indexed, uint32_t count, uint32_t instance_count, uint32_t first, int32_t offset_or_vertex, uint32_t first_instance) {
        if (!m_isRecording) return;
        
        reshade::api::resource cameraBuffer = m_cameraController->GetCameraBuffer();
        if (cameraBuffer.handle == 0) return;

        ID3D11DeviceContext* ctx = (ID3D11DeviceContext*)cmd_list->get_native();
        if (!ctx) return;

        // Check if camera buffer is bound to VS slot 0, 1, or 2
        ID3D11Buffer* nativeCamBuf = (ID3D11Buffer*)cameraBuffer.handle;
        ID3D11Buffer* vsBuffers[3] = { nullptr };
        ctx->VSGetConstantBuffers(0, 3, vsBuffers);

        int slot = -1;
        for(int i=0; i<3; ++i) {
            if (vsBuffers[i] == nativeCamBuf) {
                slot = i;
            }
            if (vsBuffers[i]) vsBuffers[i]->Release();
            if (slot != -1) break;
        }

        if (slot == -1) return;

        // Save State
        StateBlock state(ctx);

        // Loop 6 Faces
        std::vector<uint8_t> modData;

        // Create a temporary buffer for injection if not cached.
        // For performance in a real scenario, we should have a pool.
        // Here we create one per draw call which is slow but correct for logic.
        D3D11_BUFFER_DESC desc = {};
        nativeCamBuf->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        ComPtr<ID3D11Buffer> tempCB;
        ComPtr<ID3D11Device> device;
        ctx->GetDevice(&device);
        if (FAILED(device->CreateBuffer(&desc, nullptr, tempCB.GetAddressOf()))) return;

        // Get Current Depth View to reuse (assuming face render target matches size)
        ComPtr<ID3D11DepthStencilView> currentDSV;
        ctx->OMGetRenderTargets(0, nullptr, currentDSV.GetAddressOf());

        for (int i = 0; i < 6; ++i) {
            if (!m_cameraController->GetModifiedBufferData((Camera::CubeFace)i, modData)) continue;

            // Update Temp Buffer
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(ctx->Map(tempCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                memcpy(mapped.pData, modData.data(), std::min((size_t)desc.ByteWidth, modData.size()));
                ctx->Unmap(tempCB.Get(), 0);
            }

            // Bind Modified Camera
            ID3D11Buffer* cbArray[] = { tempCB.Get() };
            ctx->VSSetConstantBuffers(slot, 1, cbArray);

            // Bind Face Render Target
            // Note: We reuse current DSV. If Face Size != Screen Size, this is invalid!
            // But we init Face Size = min(w, h).
            // If the game uses a depth buffer of different size, this will fail or warn.
            // A robust solution needs a dedicated Depth Buffer for the Face Size.
            // For now, we assume the user configured the game resolution to match or we accept artifacts.

            ID3D11RenderTargetView* faceRTV = (ID3D11RenderTargetView*)m_faceRtvs[i].handle;
            ctx->OMSetRenderTargets(1, &faceRTV, currentDSV.Get()); // Re-bind DSV

            // Draw
            if (indexed) {
                ctx->DrawIndexed(count, first, offset_or_vertex); // instance_count ignored for basic draw
            } else {
                ctx->Draw(count, first);
            }
        }

        // StateBlock destructor restores state automatically
    }

    void CubemapManager::OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
        ProcessDraw(cmd_list, false, vertex_count, instance_count, first_vertex, 0, first_instance);
    }

    void CubemapManager::OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
        ProcessDraw(cmd_list, true, index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    void CubemapManager::OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
        if (!InitResources(m_width, m_height)) {
             reshade::api::resource backBuffer = swapchain->get_current_back_buffer();
             reshade::api::resource_desc desc = m_device->get_resource_desc(backBuffer);
             InitResources((uint32_t)desc.texture.width, (uint32_t)desc.texture.height);
        }

        // Copy Faces to Cube Texture (Array)
        for(int i=0; i<6; ++i) {
             m_device->copy_texture_region(m_faceTextures[i], 0, nullptr, m_cubeTexture, i, nullptr);
        }

        // Execute Compute Shader to Stitch/Project
        ID3D11DeviceContext* ctx = (ID3D11DeviceContext*)queue->get_native();

        if (m_projectionShader) {
            ctx->CSSetShader(m_projectionShader.Get(), nullptr, 0);
            ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)m_cubeSrv.handle;
            ctx->CSSetShaderResources(0, 1, &srv);
            ID3D11UnorderedAccessView* uav = (ID3D11UnorderedAccessView*)m_equirectUAV.handle;
            ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
            
            reshade::api::resource_desc desc = m_device->get_resource_desc(m_equirectTexture);
            UINT x = (desc.texture.width + 15) / 16;
            UINT y = (desc.texture.height + 15) / 16;
            ctx->Dispatch(x, y, 1);
            
            ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
            ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
            ID3D11ShaderResourceView* nullSRV[] = { nullptr };
            ctx->CSSetShaderResources(0, 1, nullSRV);
        }

        // Convert to NV12 and Encode
        // We render a full-screen quad to the NV12 targets using the Equirect texture as input
        if (m_equirectNV12) {
             // Setup Viewport
             D3D11_TEXTURE2D_DESC eqDesc;
             m_equirectNV12->GetDesc(&eqDesc);

             ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
             ctx->VSSetShader(m_convertVS.Get(), nullptr, 0);

             ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)m_equirectSRV.handle;
             ctx->PSSetShaderResources(0, 1, &srv);
             ctx->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());

             // Y Pass
             D3D11_VIEWPORT vp = {};
             vp.Width = (float)eqDesc.Width;
             vp.Height = (float)eqDesc.Height;
             vp.MaxDepth = 1.0f;
             ctx->RSSetViewports(1, &vp);
             ctx->OMSetRenderTargets(1, m_nv12Y_RTV.GetAddressOf(), nullptr);
             ctx->PSSetShader(m_convertPS_Y.Get(), nullptr, 0);
             ctx->Draw(3, 0); // Full screen triangle

             // UV Pass
             vp.Width = (float)eqDesc.Width / 2.0f;
             vp.Height = (float)eqDesc.Height / 2.0f;
             ctx->RSSetViewports(1, &vp);
             ctx->OMSetRenderTargets(1, m_nv12UV_RTV.GetAddressOf(), nullptr);
             ctx->PSSetShader(m_convertPS_UV.Get(), nullptr, 0);
             ctx->Draw(3, 0);

             // Encode
             m_encoder->EncodeFrame(m_equirectNV12.Get());

             // Cleanup
             ID3D11RenderTargetView* nullRTV = nullptr;
             ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
        }
    }
}
