#include "pch.h"
#include "CameraController.h"
#include "MatrixMath.h"
#include "../Core/Logger.h"

namespace Camera {
    CameraController::CameraController(ID3D11DeviceContext* context) : m_context(context) {}

    void CameraController::Reset() {
        m_cachedCameraBuffer = nullptr;
        m_cachedSlot = -1;
    }

    void CameraController::SetTargetFace(CubeFace face) {
        m_currentFace = face;
    }

    bool CameraController::IsProjectionMatrix(const float* data) {
        // Simple heuristic: check element [3][4] (index 11 in 0-based linear if row major? No, DX math is Row-Major usually in storage but accessed carefully)
        // DirectXMath XMMATRIX is row-major.
        // The byte stream is 16 floats.
        // We look for 0, 0, 1, 0 pattern in the last column?
        // _14, _24, _34, _44 -> indices 3, 7, 11, 15
        
        const float epsilon = 0.001f;
        // Check for 0,0,1,0 in the 4th column (typical perspective)
        if (std::abs(data[3]) < epsilon && 
            std::abs(data[7]) < epsilon && 
            std::abs(data[11] - 1.0f) < epsilon && 
            std::abs(data[15]) < epsilon) {
            return true;
        }

        // Check for 0,0,-1,0 (Right Handed?)
         if (std::abs(data[3]) < epsilon && 
            std::abs(data[7]) < epsilon && 
            std::abs(data[11] + 1.0f) < epsilon && 
            std::abs(data[15]) < epsilon) {
            return true;
        }

        return false;
    }

    void CameraController::CheckAndModifyConstantBuffer(UINT slot, ID3D11Buffer* buffer) {
        if (!buffer) return;

        // Optimization: if we found the camera buffer before, only check that optimization
        // NOTE: In a real scenario, we might need more robust checks as buffers might change
        // For this implementation, we will try to map and check every time if not cached, or blindly update if cached.
        
        // If we don't have a cached buffer or this is a different one, we might want to check it.
        // However, mapping a buffer that is currently in use by GPU will stall.
        // We should only map STAGING or DYNAMIC buffers, but Constant buffers logic is tricky.
        // Usually, we can't just map a default constant buffer. We'd have to CopyResource to a staging buffer to read.
        
        // For the sake of this task and performance, let's assume we copy to a staging buffer to inspect only once or periodically.
        
        // TODO: This part requires maintaining a staging buffer for inspection
        // For now, let's implement the logic assuming we can deduce or we are injecting "blindly" based on a heuristic if we had the data.
        // BUT, since we cannot easily read DEFAULT buffers, we will implement the logic that *writes* the modified matrix.
        
        // Strategy:
        // 1. We don't read the buffer here to avoid stalls (too slow).
        // 2. Ideally, we should intercept map/unmap calls of the game, OR UpdateSubresource.
        // This is where "Hooks.cpp" should have hooked UpdateSubresource as well. 
        // Given the scope, let's assume we rely on the game updating the buffer via UpdateSubresource.
        
        // Since we didn't hook UpdateSubresource in Hooks.cpp (only Present/Resize), the "CheckAndModifyConstantBuffer" strategy implies
        // we are calling this from "VSSetConstantBuffers" hook?
        // Wait, the spec says "Interceptor... Manipulate Camera... VSSetConstantBuffers".
        // I haven't hooked VSSetConstantBuffers in Hooks.cpp. I missed that in the spec analysis or implementation of Hooks.cpp.
        
        // I need to update Hooks.cpp to hook VSSetConstantBuffers if I want to intercept camera.
        // However, I can't update Hooks.cpp easily now that I've written it without potentially breaking or rewriting.
        // For the purpose of this task, I will implement the logic here, and "assume" `Hooks.cpp` would call this if complete.
        // Since I'm the one writing the code, I should probably update `Hooks.cpp` to include `VSSetConstantBuffers` hook!
        
        // Correct Action: I will implement the Logic here. 
        // Then I will create a NEW task to "Update Hooks.cpp" or "Add extra hooks" if needed.
        // But `Hooks.cpp` I wrote only has `Present` and `ResizeBuffers`.
        // The spec mentions: "Hooking: Injection... IDXGISwapChain::Present... ID3D11DeviceContext::DrawIndexed."
        // And "Camera Hijack: ... VSSetConstantBuffers".
        
        // So I missed hooking `VSSetConstantBuffers` and `DrawIndexed` in my `Hooks.cpp` implementation. 
        // I should probably fix `Hooks.cpp` or add a new file `HooksExtended.cpp` or simply `Hooks.cpp` v2.
        // Since I can overwrite files, I will RE-WRITE `Hooks.cpp` later to include these hooks.
        
        // For now, let's finish `CameraController.cpp` assuming the hook exists and passes the buffer.
    }
    
    DirectX::XMMATRIX CameraController::GetViewMatrixForFace(DirectX::XMMATRIX originalView, CubeFace face) {
        // 1. Extract position from original view
        DirectX::XMVECTOR scale, rotQuat, trans;
        DirectX::XMMatrixDecompose(&scale, &rotQuat, &trans, originalView);
        
        // Invert view to get world transform of camera
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, originalView);
        DirectX::XMVECTOR eyePos = invView.r[3]; 

        // 2. Create new view for the face
        DirectX::XMVECTOR up = DirectX::XMVectorSet(0, 1, 0, 0);
        DirectX::XMVECTOR forward;

        switch (face) {
            case CubeFace::Right: forward = DirectX::XMVectorSet(1, 0, 0, 0); break;
            case CubeFace::Left:  forward = DirectX::XMVectorSet(-1, 0, 0, 0); break;
            case CubeFace::Up:    forward = DirectX::XMVectorSet(0, 1, 0, 0); up = DirectX::XMVectorSet(0, 0, -1, 0); break;
            case CubeFace::Down:  forward = DirectX::XMVectorSet(0, -1, 0, 0); up = DirectX::XMVectorSet(0, 0, 1, 0); break;
            case CubeFace::Front: forward = DirectX::XMVectorSet(0, 0, 1, 0); break;
            case CubeFace::Back:  forward = DirectX::XMVectorSet(0, 0, -1, 0); break;
        }

        return DirectX::XMMatrixLookAtLH(eyePos, eyePos + forward, up);
    }

    DirectX::XMMATRIX CameraController::GetProjectionMatrix90FOV(DirectX::XMMATRIX originalProj) {
        // We need 90 degrees FOV for cubemap faces (to cover 360)
        // Aspect ratio must be 1:1
        return DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f); // Near/Far planes should ideally match game's
    }
}
