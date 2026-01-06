#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <optional>

namespace Camera {
    using namespace DirectX;

    enum class CubeFace {
        Right = 0, Left, Up, Down, Front, Back
    };

    class CameraController {
    public:
        CameraController(ID3D11DeviceContext* context);
        
        void SetTargetFace(CubeFace face);
        
        // This is called when the game tries to set constant buffers
        // We will inspect the buffer and patch it if it's a camera buffer
        void CheckAndModifyConstantBuffer(UINT slot, ID3D11Buffer* buffer);
        
        // Helps to reset state or clear cache on resize/level change
        void Reset();

    private:
        ID3D11DeviceContext* m_context;
        CubeFace m_currentFace = CubeFace::Front;
        
        // Cache needed to avoid analyzing every buffer every frame
        ID3D11Buffer* m_cachedCameraBuffer = nullptr;
        UINT m_cachedSlot = (UINT)-1;

        DirectX::XMMATRIX GetViewMatrixForFace(DirectX::XMMATRIX originalView, CubeFace face);
        DirectX::XMMATRIX GetProjectionMatrix90FOV(DirectX::XMMATRIX originalProj);
        
        bool IsProjectionMatrix(const float* data);
    };
}
