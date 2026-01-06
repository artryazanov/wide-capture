#pragma once
#include <DirectXMath.h>

namespace Camera {
    using namespace DirectX;

    // Helper class for matrix operations
    class MatrixMath {
    public:
        static bool IsProjectionMatrix(const XMMATRIX& mat) {
            // Heuristic to check if a matrix is a projection matrix
            // Check if column 4 is (0, 0, 1, 0) - Typical for perspective projection
            XMFLOAT4X4 m;
            XMStoreFloat4x4(&m, mat);
            
            // Allow some epsilon for floats
            const float e = 1e-4f;
            return (std::abs(m._14) < e && std::abs(m._24) < e && 
                    std::abs(m._34 - 1.0f) < e && std::abs(m._44) < e);
        }

        static XMMATRIX CreateViewMatrix(FXMVECTOR eye, FXMVECTOR focus, FXMVECTOR up) {
            return XMMatrixLookAtLH(eye, focus, up); // Assuming LH coordinate system as default for many DX engines
        }

        static XMMATRIX CreateProjectionMatrix(float fov, float aspectRatio, float nearZ, float farZ) {
            return XMMatrixPerspectiveFovLH(fov, aspectRatio, nearZ, farZ);
        }
    };
}
