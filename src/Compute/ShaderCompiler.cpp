#include "pch.h"
#include "ShaderCompiler.h"
#include "../Core/Logger.h"

namespace Compute {
    HRESULT ShaderCompiler::CompileComputeShader(
        ID3D11Device* device, 
        const std::wstring& filename,
        const std::string& entryPoint, 
        ID3D11ComputeShader** ppShader,
        ID3D10Blob** ppBlob
    ) {
        Microsoft::WRL::ComPtr<ID3D10Blob> shaderBlob;
        Microsoft::WRL::ComPtr<ID3D10Blob> errorBlob;
        
        DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG;
#endif

        HRESULT hr = D3DCompileFromFile(
            filename.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint.c_str(),
            "cs_5_0", // Compute Shader 5.0
            flags,
            0,
            shaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );

        if (FAILED(hr)) {
            if (errorBlob) {
                LOG_ERROR("Shader Compilation Error: ", (char*)errorBlob->GetBufferPointer());
            } else {
                LOG_ERROR("Shader Compilation Failed. HR: ", std::hex, hr);
            }
            return hr;
        }

        hr = device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, ppShader);
        
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create Compute Shader. HR: ", std::hex, hr);
            return hr;
        }

        if (ppBlob) {
            *ppBlob = shaderBlob.Detach();
        }

        return S_OK;
    }
}
