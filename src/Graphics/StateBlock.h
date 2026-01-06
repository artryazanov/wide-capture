#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace Graphics {
    using Microsoft::WRL::ComPtr;

    class StateBlock {
    public:
        StateBlock(ID3D11DeviceContext* context);
        ~StateBlock();

        void Capture();
        void Restore();

    private:
        ID3D11DeviceContext* m_context;

        // Input Assembler
        ComPtr<ID3D11InputLayout> m_inputLayout;
        D3D11_PRIMITIVE_TOPOLOGY m_topology;
        ComPtr<ID3D11Buffer> m_indexBuffer;
        DXGI_FORMAT m_indexBufferFormat;
        UINT m_indexBufferOffset;
        ComPtr<ID3D11Buffer> m_vertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        UINT m_vertexStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        UINT m_vertexOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];

        // Rasterizer
        ComPtr<ID3D11RasterizerState> m_rasterizerState;
        ComPtr<ID3D11Buffer> m_vsConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        ComPtr<ID3D11VertexShader> m_vertexShader;
        
        // Pixel Shader
        ComPtr<ID3D11PixelShader> m_pixelShader;
        ComPtr<ID3D11Buffer> m_psConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        ComPtr<ID3D11ShaderResourceView> m_psSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
        ComPtr<ID3D11SamplerState> m_psSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];

        // Output Merger
        ComPtr<ID3D11BlendState> m_blendState;
        FLOAT m_blendFactor[4];
        UINT m_sampleMask;
        ComPtr<ID3D11DepthStencilState> m_depthStencilState;
        UINT m_stencilRef;
        ComPtr<ID3D11RenderTargetView> m_renderTargetViews[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        ComPtr<ID3D11DepthStencilView> m_depthStencilView;
        D3D11_VIEWPORT m_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT m_numViewports;

        // Compute Shader (if needed for compute tasks)
        ComPtr<ID3D11ComputeShader> m_computeShader;
        ComPtr<ID3D11UnorderedAccessView> m_csUAVs[D3D11_1_UAV_SLOT_COUNT];
        ComPtr<ID3D11ShaderResourceView> m_csSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
        ComPtr<ID3D11Buffer> m_csConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        ComPtr<ID3D11SamplerState> m_csSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
    };
}
