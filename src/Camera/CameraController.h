#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include <unordered_map>
#include "../Graphics/DX11Proxy.h"

namespace Camera {
    using namespace DirectX;
    using Microsoft::WRL::ComPtr;

    enum class CubeFace {
        Right = 0, Left, Up, Down, Front, Back
    };

    class CameraController {
    public:
        CameraController(ID3D11DeviceContext* context);
        
        void SetTargetFace(CubeFace face);
        void Reset();
        
        // Return replacement buffer if this buffer is the camera buffer
        ID3D11Buffer* CheckAndGetReplacementBuffer(ID3D11DeviceContext* context, ID3D11Buffer* originalBuf);
        void OnUnmap(ID3D11DeviceContext* pContext, ID3D11Resource* pResource);
        
        void UpdateStateBlock(ID3D11DeviceContext* pContext);
        // Hooks tracking
        void OnMap(ID3D11Resource* resource, D3D11_MAPPED_SUBRESOURCE* mapped);

        void OnUpdateSubresource(ID3D11Resource* resource, const void* data, const D3D11_BOX* box);
        


    private:
        ID3D11Buffer* GetReplacementBuffer(ID3D11Resource* originalBuffer, const float* originalData);

        ID3D11DeviceContext* m_context;
        CubeFace m_currentFace = CubeFace::Front;

        // Tracking state
        struct BufferState {
            std::vector<uint8_t> cpuData;
            bool isDirty = true;
            void* mappedPtr = nullptr;
            bool isCamera = false;
            
            // Unique replacement buffer for this original buffer
            ComPtr<ID3D11Buffer> replacementBuffer; 
        };
        
        // Map ID3D11Buffer pointer -> State
        std::unordered_map<ID3D11Resource*, BufferState> m_bufferCache;
        
        // Staging buffer for force-reading GPU buffers
        ComPtr<ID3D11Buffer> m_readbackBuffer;
        void ForceReadBuffer(ID3D11Buffer* buffer); 
        
        DirectX::XMMATRIX GetViewMatrixForFace(DirectX::XMMATRIX originalView, CubeFace face, bool isRH);
        DirectX::XMMATRIX GetProjectionMatrix90FOV(DirectX::XMMATRIX originalProj);
        bool IsProjectionMatrix(const float* data);
        bool IsRightHandedProjection(const float* data);
        bool IsViewMatrix(const float* data, bool* outIsTransposed = nullptr);
        
        std::mutex m_cacheMutex;
        bool m_isRH = false; // Default to LH
    };
}
