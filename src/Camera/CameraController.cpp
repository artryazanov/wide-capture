#include "pch.h"
#include "CameraController.h"
#include "../Core/Logger.h"

namespace Camera {

    CameraController::CameraController() {}

    void CameraController::OnUpdateBuffer(reshade::api::resource resource, const void* data, uint64_t size) {
        if (size < 64) return; // Too small for a matrix

        std::lock_guard<std::mutex> lock(m_mutex);
        
        uint64_t handle = resource.handle;
        auto& state = m_bufferCache[handle];
        
        // Update cache
        if (state.data.size() != size) state.data.resize(size);
        memcpy(state.data.data(), data, size);

        const float* floatData = (const float*)data;
        size_t floatCount = size / sizeof(float);

        // Heuristic detection if not already detected
        // Note: In ReShade we might want to re-verify occasionally, but for now stick to "once detected, stick to it"
        // unless we want to support camera switching. Let's re-scan if it *was* a camera to update matrices.
        
        bool foundView = false;
        bool foundProj = false;

        // Scan for View Matrix
        for (size_t i = 0; i <= floatCount - 16; i += 4) {
            bool transposed = false;
            if (IsViewMatrix(floatData + i, &transposed)) {
                state.viewMatrixOffset = (int)i;
                state.isCamera = true;

                m_isTransposed = transposed;
                DirectX::XMMATRIX viewMat = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)(floatData + i));
                if (transposed) viewMat = DirectX::XMMatrixTranspose(viewMat);

                m_lastGameView = viewMat;

                if (!m_upDetected) {
                    DetectWorldUp(viewMat);
                }

                m_cameraBuffer = resource; // Set as active camera buffer
                foundView = true;
                break; // Assume one view matrix per buffer for simplicity
            }
        }

        // Scan for Projection Matrix
        for (size_t i = 0; i <= floatCount - 16; i += 4) {
            if (IsProjectionMatrix(floatData + i)) {
                state.projMatrixOffset = (int)i;
                state.isCamera = true;

                m_isRH = IsRightHandedProjection(floatData + i);
                m_lastGameProj = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)(floatData + i));

                m_cameraBuffer = resource;
                foundProj = true;
                break;
            }
        }
    }

    bool CameraController::GetModifiedBufferData(CubeFace face, std::vector<uint8_t>& outputData) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cameraBuffer.handle == 0) return false;

        auto it = m_bufferCache.find(m_cameraBuffer.handle);
        if (it == m_bufferCache.end()) return false;

        auto& state = it->second;
        outputData = state.data; // Copy original data

        float* outFloats = (float*)outputData.data();
        size_t floatCount = outputData.size() / sizeof(float);

        // Replace View Matrix
        if (state.viewMatrixOffset >= 0 && (size_t)(state.viewMatrixOffset + 16) <= floatCount) {
            DirectX::XMMATRIX newView = GetViewMatrixForFace(face);
            if (m_isTransposed) newView = DirectX::XMMatrixTranspose(newView);

            DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)(outFloats + state.viewMatrixOffset), newView);
        }

        // Replace Projection Matrix (Force 90 degree FOV)
        if (state.projMatrixOffset >= 0 && (size_t)(state.projMatrixOffset + 16) <= floatCount) {
            DirectX::XMMATRIX newProj;
            if (m_isRH) {
                newProj = DirectX::XMMatrixPerspectiveFovRH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
            } else {
                newProj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
            }
            DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)(outFloats + state.projMatrixOffset), newProj);
        }

        return true;
    }

    DirectX::XMMATRIX CameraController::GetViewMatrixForFace(CubeFace face) {
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, m_lastGameView);
        DirectX::XMVECTOR eyePos = invView.r[3];

        bool isZUp = (std::abs(DirectX::XMVectorGetZ(m_worldUp)) > 0.9f);

        DirectX::XMVECTOR vRight, vLeft, vUp, vDown, vFront, vBack;

        if (isZUp) {
             // Z-Up System
             vRight = DirectX::XMVectorSet(1, 0, 0, 0);
             vLeft  = DirectX::XMVectorSet(-1, 0, 0, 0);
             vUp    = DirectX::XMVectorSet(0, 0, 1, 0);
             vDown  = DirectX::XMVectorSet(0, 0, -1, 0);
             vFront = DirectX::XMVectorSet(0, 1, 0, 0);
             vBack  = DirectX::XMVectorSet(0, -1, 0, 0);
        } else {
             // Y-Up System
             vRight = DirectX::XMVectorSet(1, 0, 0, 0);
             vLeft  = DirectX::XMVectorSet(-1, 0, 0, 0);
             vUp    = DirectX::XMVectorSet(0, 1, 0, 0);
             vDown  = DirectX::XMVectorSet(0, -1, 0, 0);
             if (m_isRH) {
                 vFront = DirectX::XMVectorSet(0, 0, -1, 0);
                 vBack  = DirectX::XMVectorSet(0, 0, 1, 0);
             } else {
                 vFront = DirectX::XMVectorSet(0, 0, 1, 0);
                 vBack  = DirectX::XMVectorSet(0, 0, -1, 0);
             }
        }

        DirectX::XMVECTOR targetDir;
        DirectX::XMVECTOR upDir = m_worldUp;

        switch (face) {
            case CubeFace::Right: targetDir = vRight; break;
            case CubeFace::Left:  targetDir = vLeft; break;
            case CubeFace::Up:    targetDir = vUp; upDir = vFront; break;
            case CubeFace::Down:  targetDir = vDown; upDir = DirectX::XMVectorNegate(vFront); break;
            case CubeFace::Front: targetDir = vFront; break;
            case CubeFace::Back:  targetDir = vBack; break;
        }

        if (m_isRH) return DirectX::XMMatrixLookAtRH(eyePos, DirectX::XMVectorAdd(eyePos, targetDir), upDir);
        else        return DirectX::XMMatrixLookAtLH(eyePos, DirectX::XMVectorAdd(eyePos, targetDir), upDir);
    }

    bool CameraController::IsProjectionMatrix(const float* data) {
        const float epsilon = 0.1f;
        // Check for projection patterns (0s in specific spots)
        // [ x 0 0 0 ]
        // [ 0 x 0 0 ]
        // [ 0 0 x x ]
        // [ 0 0 x 0 ]
        if (std::abs(data[1]) > epsilon || std::abs(data[2]) > epsilon || std::abs(data[3]) > epsilon) return false;
        if (std::abs(data[4]) > epsilon || std::abs(data[6]) > epsilon || std::abs(data[7]) > epsilon) return false;
        if (std::abs(data[15]) > epsilon) return false;
        
        // Check w components
        if (std::abs(data[11] - 1.0f) > epsilon && std::abs(data[11] + 1.0f) > epsilon) return false;

        return true;
    }

    bool CameraController::IsViewMatrix(const float* data, bool* outIsTransposed) {
        const float epsilon = 0.1f;
        // Row Major: [x x x 0], [x x x 0], [x x x 0], [x x x 1]
        bool rowMajor = (std::abs(data[3]) < epsilon && std::abs(data[7]) < epsilon && std::abs(data[11]) < epsilon && std::abs(data[15] - 1.0f) < epsilon);
        // Col Major (Transposed): [x x x x], [x x x x], [x x x x], [0 0 0 1]
        bool colMajor = (std::abs(data[12]) < epsilon && std::abs(data[13]) < epsilon && std::abs(data[14]) < epsilon && std::abs(data[15] - 1.0f) < epsilon);

        if (outIsTransposed) *outIsTransposed = colMajor;

        // To distinguish from World Matrix, we check if it is orthogonal (rotation part).
        // View Matrix rotation part is orthogonal. World Matrix can be scaled.
        // For now, heuristic is weak but matches original code intent.

        return rowMajor || colMajor;
    }

    bool CameraController::IsRightHandedProjection(const float* data) {
        // [2][3] (index 11) is -1 for RH, 1 for LH usually
        return (data[11] < -0.9f);
    }

    void CameraController::DetectWorldUp(DirectX::XMMATRIX viewMat) {
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, viewMat);
        DirectX::XMVECTOR up = invView.r[1];
        up = DirectX::XMVector3Normalize(up);

        float y = DirectX::XMVectorGetY(up);
        float z = DirectX::XMVectorGetZ(up);

        if (std::abs(z) > std::abs(y)) {
            if (z > 0) m_worldUp = DirectX::XMVectorSet(0, 0, 1, 0);
            else       m_worldUp = DirectX::XMVectorSet(0, 0, -1, 0);
            LOG_INFO("Detected Z-Up World");
        } else {
            if (y > 0) m_worldUp = DirectX::XMVectorSet(0, 1, 0, 0);
            else       m_worldUp = DirectX::XMVectorSet(0, -1, 0, 0);
            LOG_INFO("Detected Y-Up World");
        }
        m_upDetected = true;
    }
}
