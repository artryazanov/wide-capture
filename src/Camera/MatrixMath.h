#pragma once
#include <DirectXMath.h>

namespace Camera {
    using namespace DirectX;

    // Helper class for matrix operations
    class MatrixMath {
    public:
        static bool IsProjectionMatrix(const DirectX::XMMATRIX& mat) {
            // Heuristic to check if a matrix is a projection matrix
            // Check if column 4 is (0, 0, 1, 0) - Typical for perspective projection
            DirectX::XMFLOAT4X4 m;
            DirectX::XMStoreFloat4x4(&m, mat);
            
            // Allow some epsilon for floats
            const float e = 1e-4f;
            return (std::abs(m._14) < e && std::abs(m._24) < e && 
                    std::abs(m._34 - 1.0f) < e && std::abs(m._44) < e);
        }

        static DirectX::XMMATRIX CreateViewMatrix(DirectX::FXMVECTOR eye, DirectX::FXMVECTOR focus, DirectX::FXMVECTOR up) {
            return DirectX::XMMatrixLookAtLH(eye, focus, up); // Assuming LH coordinate system as default for many DX engines
        }

        static DirectX::XMMATRIX CreateProjectionMatrix(float fov, float aspectRatio, float nearZ, float farZ) {
            return DirectX::XMMatrixPerspectiveFovLH(fov, aspectRatio, nearZ, farZ);
        }
    };
}
