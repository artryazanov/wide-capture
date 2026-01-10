#pragma once
#include <reshade.hpp>
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <mutex>
#include <map>

namespace Camera {

    enum class CubeFace {
        Right = 0,
        Left = 1,
        Up = 2,
        Down = 3,
        Front = 4,
        Back = 5
    };

    struct ConstantBufferState {
        std::vector<uint8_t> data;
        bool isCamera = false;
        int viewMatrixOffset = -1;
        int projMatrixOffset = -1;
    };

    class CameraController {
    public:
        CameraController();
        
        void OnUpdateBuffer(reshade::api::resource resource, const void* data, uint64_t size);
        
        // Returns the handle of the buffer detected as the camera constant buffer
        reshade::api::resource GetCameraBuffer() const { return m_cameraBuffer; }
        
        // Calculates the View Matrix for a specific face based on the last detected game view
        DirectX::XMMATRIX GetViewMatrixForFace(CubeFace face);

        // Fills the provided buffer with the modified constant buffer data for the given face
        // Returns true if successful (and outputData is filled)
        bool GetModifiedBufferData(CubeFace face, std::vector<uint8_t>& outputData);

    private:
        bool IsProjectionMatrix(const float* data);
        bool IsViewMatrix(const float* data, bool* outIsTransposed);
        bool IsRightHandedProjection(const float* data);
        void DetectWorldUp(DirectX::XMMATRIX viewMat);

        reshade::api::resource m_cameraBuffer = { 0 };
        std::mutex m_mutex;
        std::map<uint64_t, ConstantBufferState> m_bufferCache; // Key is resource handle value

        DirectX::XMMATRIX m_lastGameView;
        DirectX::XMMATRIX m_lastGameProj;
        DirectX::XMVECTOR m_worldUp = DirectX::XMVectorSet(0, 1, 0, 0);
        bool m_upDetected = false;
        bool m_isRH = false; // Right-Handed
        bool m_isTransposed = false; // Matrix layout in buffer
    };
}
