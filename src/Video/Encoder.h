#pragma once
#include <d3d11.h>
#include <string>

namespace Video {
    class Encoder {
    public:
        virtual ~Encoder() = default;
        virtual bool Initialize(ID3D11Device* pDevice, int width, int height, int fps, const std::string& filename) = 0;
        virtual void EncodeFrame(ID3D11Texture2D* pSourceTexture) = 0;
        virtual void Finish() = 0;
    };
}
