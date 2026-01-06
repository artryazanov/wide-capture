#include "pch.h"
#include "StateBlock.h"

namespace Graphics {
    StateBlock::StateBlock(ID3D11DeviceContext* context) : m_context(context) {
        Capture();
    }

    StateBlock::~StateBlock() {
        Restore();
    }

    void StateBlock::Capture() {
        // IA
        m_context->IAGetInputLayout(m_inputLayout.GetAddressOf());
        m_context->IAGetPrimitiveTopology(&m_topology);
        m_context->IAGetIndexBuffer(m_indexBuffer.GetAddressOf(), &m_indexBufferFormat, &m_indexBufferOffset);
        m_context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, m_vertexBuffers[0].GetAddressOf(), m_vertexStrides, m_vertexOffsets);

        // RS
        m_context->RSGetState(m_rasterizerState.GetAddressOf());
        m_numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        m_context->RSGetViewports(&m_numViewports, m_viewports);

        // VS
        m_context->VSGetShader(m_vertexShader.GetAddressOf(), nullptr, nullptr);
        m_context->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, m_vsConstantBuffers[0].GetAddressOf());

        // PS
        m_context->PSGetShader(m_pixelShader.GetAddressOf(), nullptr, nullptr);
        m_context->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, m_psConstantBuffers[0].GetAddressOf());
        m_context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, m_psSRVs[0].GetAddressOf());
        m_context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, m_psSamplers[0].GetAddressOf());

        // OM
        m_context->OMGetBlendState(m_blendState.GetAddressOf(), m_blendFactor, &m_sampleMask);
        m_context->OMGetDepthStencilState(m_depthStencilState.GetAddressOf(), &m_stencilRef);
        m_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, m_renderTargetViews[0].GetAddressOf(), m_depthStencilView.GetAddressOf());

        // CS
        m_context->CSGetShader(m_computeShader.GetAddressOf(), nullptr, nullptr);
        m_context->CSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, m_csConstantBuffers[0].GetAddressOf());
        m_context->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, m_csSRVs[0].GetAddressOf());
        m_context->CSGetUnorderedAccessViews(0, D3D11_1_UAV_SLOT_COUNT, m_csUAVs[0].GetAddressOf());
        m_context->CSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, m_csSamplers[0].GetAddressOf());
    }

    void StateBlock::Restore() {
        // IA
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(m_topology);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), m_indexBufferFormat, m_indexBufferOffset);
        // ComPtr array to raw pointer array conversion
        ID3D11Buffer* vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        for(int i=0; i<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i) vbs[i] = m_vertexBuffers[i].Get();
        m_context->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vbs, m_vertexStrides, m_vertexOffsets);

        // RS
        m_context->RSSetState(m_rasterizerState.Get());
        m_context->RSSetViewports(m_numViewports, m_viewports);

        // VS
        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        ID3D11Buffer* vscbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        for(int i=0; i<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) vscbs[i] = m_vsConstantBuffers[i].Get();
        m_context->VSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, vscbs);

        // PS
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        ID3D11Buffer* pscbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        for(int i=0; i<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) pscbs[i] = m_psConstantBuffers[i].Get();
        m_context->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, pscbs);
        
        ID3D11ShaderResourceView* pssrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
        for(int i=0; i<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) pssrvs[i] = m_psSRVs[i].Get();
        m_context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, pssrvs);

        ID3D11SamplerState* pssamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
        for(int i=0; i<D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) pssamplers[i] = m_psSamplers[i].Get();
        m_context->PSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, pssamplers);

        // OM
        m_context->OMSetBlendState(m_blendState.Get(), m_blendFactor, m_sampleMask);
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        for(int i=0; i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) rtvs[i] = m_renderTargetViews[i].Get();
        m_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, m_depthStencilView.Get());
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), m_stencilRef);

        // CS
        m_context->CSSetShader(m_computeShader.Get(), nullptr, 0);
        ID3D11Buffer* cscbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        for(int i=0; i<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) cscbs[i] = m_csConstantBuffers[i].Get();
        m_context->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, cscbs);
        
        ID3D11ShaderResourceView* cssrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
        for(int i=0; i<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) cssrvs[i] = m_csSRVs[i].Get();
        m_context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, cssrvs);

        ID3D11UnorderedAccessView* csuavs[D3D11_1_UAV_SLOT_COUNT];
        for(int i=0; i<D3D11_1_UAV_SLOT_COUNT; ++i) csuavs[i] = m_csUAVs[i].Get();
        // Careful with UAVs, we often set them via CSSetUnorderedAccessViews but with initial counts
        UINT initialCounts[D3D11_1_UAV_SLOT_COUNT] = { (UINT)-1 }; // Keep current counters
        m_context->CSSetUnorderedAccessViews(0, D3D11_1_UAV_SLOT_COUNT, csuavs, nullptr); // nullptr means maintain counters usually, or use previous. Actually there is no easy "maintain", we just set.
        
        ID3D11SamplerState* cssamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
        for(int i=0; i<D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) cssamplers[i] = m_csSamplers[i].Get();
        m_context->CSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, cssamplers);
    }
}
