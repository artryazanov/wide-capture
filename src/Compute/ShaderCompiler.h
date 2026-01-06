#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace Compute {
    class ShaderCompiler {
    public:
        static HRESULT CompileComputeShader(
            ID3D11Device* device, 
            const std::wstring& filename,
            const std::string& entryPoint, 
            ID3D11ComputeShader** ppShader,
            ID3D10Blob** ppBlob = nullptr
        );
    };
}
