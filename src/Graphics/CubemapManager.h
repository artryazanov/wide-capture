#pragma once
#include <reshade.hpp>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include "../Camera/CameraController.h"
#include "../Video/FFmpegBackend.h"

namespace Graphics {

    class CubemapManager {
    public:
        CubemapManager(reshade::api::device* device);
        ~CubemapManager();

        void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain);
        void OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
        void OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
        void OnUpdateBuffer(reshade::api::device* device, reshade::api::resource resource, const void* data, uint64_t size);
        void OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline);

    private:
        bool InitResources(uint32_t width, uint32_t height);
        void DestroyResources();
        
        void ProcessDraw(reshade::api::command_list* cmd_list, auto drawCallback);

        reshade::api::device* m_device = nullptr;
        std::unique_ptr<Camera::CameraController> m_cameraController;
        std::unique_ptr<Video::FFmpegBackend> m_encoder;

        // Resources
        reshade::api::resource m_faceTextures[6] = {};
        reshade::api::resource_view m_faceRtvs[6] = {};
        reshade::api::resource_view m_faceSrvs[6] = {};
        
        reshade::api::resource m_cubeTexture = {};
        reshade::api::resource_view m_cubeSrv = {};

        reshade::api::resource m_equirectTexture = {};
        reshade::api::resource_view m_equirectUAV = {};
        reshade::api::resource_view m_equirectSRV = {};

        // Native Interop for FFmpeg (NV12)
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_equirectNV12;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_nv12Y_RTV;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_nv12UV_RTV;

        // Shaders (Native D3D11 for now as ReShade doesn't provide easy runtime compilation)
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_projectionShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_convertVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_convertPS_Y;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_convertPS_UV;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearSampler;

        bool m_isRecording = true;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_faceSize = 0;

        // Pipeline tracking
        reshade::api::pipeline_layout m_currentPipelineLayout = { 0 };
        // We need to track if the camera buffer is bound. ReShade doesn't give easy access to "currently bound buffers"
        // without tracking 'push_descriptors' or 'bind_descriptor_tables'.
        // Simplified: we will try to find the slot index if possible, or just scan binds.
        // Actually, detecting if the *current* draw call uses the camera buffer is hard without tracking bindings.
        // For now, we will assume if the CameraController has identified a buffer, we check if it is active.
        // BUT: ReShade's event `draw` doesn't tell us what resources are bound.
        // We must implement `push_descriptors` / `bind_descriptor_tables` tracking in the manager.

        bool m_isCameraBufferBound = false;
    };
}
