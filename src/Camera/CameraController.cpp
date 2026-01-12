#include "pch.h"
#include "CameraController.h"
#include "MatrixMath.h"
#include "../Core/Logger.h"

namespace Camera {
    CameraController::CameraController(ID3D11DeviceContext* context) : m_context(context) {}

    void CameraController::Reset() {
        m_bufferCache.clear();
        m_upDetected = false;
        m_worldUp = DirectX::XMVectorSet(0, 1, 0, 0);
    }

    void CameraController::SetTargetFace(CubeFace face) {
        m_currentFace = face;
    }

    void CameraController::SetBypass(bool bypass) {
        m_bypass = bypass;
    }

    // Thread-local guard to prevent recursion and self-tracking
    static thread_local bool s_IgnoreHooks = false;

    struct ScopedHookIgnore {
        ScopedHookIgnore() { s_IgnoreHooks = true; }
        ~ScopedHookIgnore() { s_IgnoreHooks = false; }
    };

    void CameraController::OnUpdateSubresource(ID3D11Resource* resource, const void* data, const D3D11_BOX* box) {
        if (s_IgnoreHooks) return; // Ignore our own calls

        D3D11_RESOURCE_DIMENSION dim;
        resource->GetType(&dim);
        if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return;
        
        std::lock_guard<std::mutex> lock(m_cacheMutex);

        auto& state = m_bufferCache[resource];
        
        // If new or empty, get size
        if (state.cpuData.empty()) {
            ID3D11Buffer* buf = (ID3D11Buffer*)resource;
            D3D11_BUFFER_DESC desc;
            buf->GetDesc(&desc);
            if (desc.ByteWidth > 4096) return; // Ignore huge buffers
            state.cpuData.resize(desc.ByteWidth);
        }
        
        if (state.cpuData.empty()) return;

        if (data) {
            if (box) {
                // Partial Update
                UINT width = box->right - box->left;
                if (box->left + width <= state.cpuData.size()) {
                     memcpy(state.cpuData.data() + box->left, data, width);
                     state.isDirty = true;
                }
            } else {
                // Full Update
                memcpy(state.cpuData.data(), data, state.cpuData.size());
                state.isDirty = true;
            }
        }
    }

    void CameraController::OnMap(ID3D11Resource* resource, D3D11_MAPPED_SUBRESOURCE* mapped) {
         if (s_IgnoreHooks) return; // Ignore our own maps

         D3D11_RESOURCE_DIMENSION dim;
         resource->GetType(&dim);
         if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return;

         if (m_readbackBuffer && resource == m_readbackBuffer.Get()) return;

         std::lock_guard<std::mutex> lock(m_cacheMutex);
         m_bufferCache[resource].mappedPtr = mapped->pData;
    }

    void CameraController::OnUnmap(ID3D11DeviceContext* /*pContext*/, ID3D11Resource* /*resource*/) {
         // Reverted to empty/safe for stability.
    }

    // --- logic ---

    ID3D11Buffer* CameraController::CheckAndGetReplacementBuffer(ID3D11DeviceContext* /*context*/, ID3D11Buffer* originalBuffer) {
        if (!originalBuffer) return nullptr;
        // If bypass is on (Player Frame), do not interfere
        if (m_bypass) return nullptr;
        
        std::lock_guard<std::mutex> lock(m_cacheMutex);

        auto it = m_bufferCache.find(originalBuffer);
        
        if (it == m_bufferCache.end() || it->second.cpuData.empty()) {
             ForceReadBuffer(originalBuffer);
             it = m_bufferCache.find(originalBuffer);
             if (it == m_bufferCache.end() || it->second.cpuData.empty()) {
                 return nullptr;
             }
        }

        auto& state = it->second;
        const float* data = (const float*)state.cpuData.data();
        size_t floatCount = state.cpuData.size() / sizeof(float);
        
        if (floatCount < 16) return nullptr;

        // 2. Detection (Heuristic)
        // Check stored flag first
        bool isCamera = state.isCamera;
        
        // If not yet detected, scan
        if (!isCamera) {
            // Heuristic Check
            for (size_t i = 0; i <= floatCount - 16; i += 4) { 
                 if (IsProjectionMatrix(data + i)) {
                      state.isCamera = true;
                      state.projMatrixOffset = (int)i; // Cache Offset
                      isCamera = true;
                      break;
                 }
                 
                 if (IsViewMatrix(data + i, nullptr)) {
                      state.isCamera = true;
                      state.viewMatrixOffset = (int)i; // Cache Offset
                      isCamera = true;
                      break;
                 }
            }
        }
        
        if (isCamera) {
             return GetReplacementBuffer(originalBuffer, data);
        }
        
        return nullptr;
    }

    ID3D11Buffer* CameraController::GetReplacementBuffer(ID3D11Resource* originalBuffer, const float* originalData) {
        if (!originalBuffer) return nullptr;
        if (m_bypass) return nullptr;
        
        auto& state = m_bufferCache[originalBuffer];
        size_t size = state.cpuData.size();
        size_t floatCount = size / sizeof(float);
        
        // Ensure replacement buffer exists and is large enough
        if (state.replacementBuffer) {
             D3D11_BUFFER_DESC currentDesc;
             state.replacementBuffer->GetDesc(&currentDesc);
             if (currentDesc.ByteWidth < size) {
                 state.replacementBuffer.Reset();
             }
        }

        if (!state.replacementBuffer) {
             D3D11_BUFFER_DESC desc = {};
             desc.ByteWidth = (UINT)size; 
             if (desc.ByteWidth % 16 != 0) desc.ByteWidth = ((desc.ByteWidth + 15) / 16) * 16;
             
             desc.Usage = D3D11_USAGE_DYNAMIC;
             desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
             desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
             
             ComPtr<ID3D11Device> device;
             m_context->GetDevice(device.GetAddressOf());
             
             if (FAILED(device->CreateBuffer(&desc, nullptr, state.replacementBuffer.GetAddressOf()))) {
                 LOG_ERROR("Failed to create replacement buffer for ", originalBuffer);
                 return nullptr;
             }
        }
        
        D3D11_MAPPED_SUBRESOURCE mapped;
        {
            ScopedHookIgnore ignore; 
            if (SUCCEEDED(m_context->Map(state.replacementBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                 memcpy(mapped.pData, originalData, size);
                 float* outData = (float*)mapped.pData;
                 
                 // Use Cached Offsets if available
                 if (state.projMatrixOffset >= 0 && (state.projMatrixOffset + 16) <= floatCount) {
                     float* searchPtr = outData + state.projMatrixOffset;
                     bool isRH = IsRightHandedProjection((const float*)(originalData + state.projMatrixOffset));
                     m_isRH = isRH;

                     DirectX::XMMATRIX newProj;
                     if (isRH) {
                          newProj = DirectX::XMMatrixPerspectiveFovRH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
                     } else {
                          newProj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
                     }
                     DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)searchPtr, newProj);
                 }

                 if (state.viewMatrixOffset >= 0 && (state.viewMatrixOffset + 16) <= floatCount) {
                     float* searchPtr = outData + state.viewMatrixOffset;
                     bool isTransposed = false;
                     // Double check orientation
                     IsViewMatrix((const float*)(originalData + state.viewMatrixOffset), &isTransposed);

                     DirectX::XMMATRIX viewMat = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)(originalData + state.viewMatrixOffset));
                     if (isTransposed) viewMat = DirectX::XMMatrixTranspose(viewMat);

                     // 1. Detect World Up from Game View (Once)
                     if (!m_upDetected) DetectWorldUp(viewMat);

                     // 2. Generate New View using World Aligned logic
                     DirectX::XMMATRIX newView = GetViewMatrixForFace(viewMat, m_currentFace, m_isRH);

                     if (isTransposed) newView = DirectX::XMMatrixTranspose(newView);

                     DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)searchPtr, newView);
                 }

                 // If no offsets cached (shouldn't happen if isCamera=true), we could scan as fallback,
                 // but for now we rely on the cached offsets from detection phase.

                 m_context->Unmap(state.replacementBuffer.Get(), 0);
            }
        }
        
        return state.replacementBuffer.Get();
    }
    
    void CameraController::DetectWorldUp(DirectX::XMMATRIX viewMat) {
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, viewMat);

        // In Inverse View, the columns are Right, Up, Look, Eye.
        // Row 1 (index 0-3) is Right (or index 0 of rows if transposed).
        // Since we loaded it into XMMATRIX, it handles row/col major if we respect logic.
        // XMMatrixInverse returns inverse.

        // Up Vector is the second row of the inverse matrix (since DX math uses Row Vectors, but wait...)
        // In DX Math (Row Major):
        // View = [ R.x  U.x  L.x  0 ]
        //        [ R.y  U.y  L.y  0 ] ...
        // No, standard View Matrix constructs:
        // Rx Ry Rz -dot(R,E)
        // Ux Uy Uz -dot(U,E)
        // Lx Ly Lz -dot(L,E)
        // 0  0  0  1
        //
        // Inverse View (Camera World Matrix):
        // Rx Ux Lx Ex
        // Ry Uy Ly Ey
        // Rz Uz Lz Ez
        // 0  0  0  1
        //
        // So Up vector is the 2nd row (r[1]).

        DirectX::XMVECTOR up = invView.r[1];

        // Normalize just in case
        up = DirectX::XMVector3Normalize(up);

        float y = DirectX::XMVectorGetY(up);
        float z = DirectX::XMVectorGetZ(up);

        if (std::abs(z) > std::abs(y)) {
            // Z-Up (e.g. 0,0,1)
            if (z > 0) m_worldUp = DirectX::XMVectorSet(0, 0, 1, 0);
            else       m_worldUp = DirectX::XMVectorSet(0, 0, -1, 0);
            LOG_INFO("Detected Z-Up World");
        } else {
            // Y-Up (e.g. 0,1,0)
            if (y > 0) m_worldUp = DirectX::XMVectorSet(0, 1, 0, 0);
            else       m_worldUp = DirectX::XMVectorSet(0, -1, 0, 0);
            LOG_INFO("Detected Y-Up World");
        }
        m_upDetected = true;
    }

    DirectX::XMMATRIX CameraController::GetViewMatrixForFace(DirectX::XMMATRIX originalView, CubeFace face, bool isRH) {
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, originalView);
        DirectX::XMVECTOR eyePos = invView.r[3]; 

        // World Up vector (Auto-detected)
        DirectX::XMVECTOR worldUp = m_worldUp;

        // To construct the 6 faces aligned to World, we need a standard set of directions.
        // If Y-Up:
        //   Front: +Z (LH) or -Z (RH)
        //   Right: +X
        //   Up:    +Y
        // If Z-Up:
        //   Front: +Y? or +X? Usually +Y is North/Forward in Z-Up systems (Unreal).
        //   Right: +X
        //   Up:    +Z

        // Let's define "North" as +Z (if Y-up) or +Y (if Z-up).
        // Actually, for the panorama to be stable, we just need *consistent* orthogonal basis.
        // We will stick to the Enum mapping:
        // Face 0 (Right): +X
        // Face 1 (Left):  -X
        // Face 2 (Up):    +Y (or +Z if Z-up)
        // Face 3 (Down):  -Y (or -Z if Z-up)
        // Face 4 (Front): +Z (or +Y if Z-up)
        // Face 5 (Back):  -Z (or -Y if Z-up)

        bool isZUp = (std::abs(DirectX::XMVectorGetZ(worldUp)) > 0.9f);

        DirectX::XMVECTOR vRight, vLeft, vUp, vDown, vFront, vBack;

        if (isZUp) {
             // Z-Up System
             // Up is +Z
             // Front is +Y (Standard for Z-Up games like Unreal)
             // Right is +X
             vRight = DirectX::XMVectorSet(1, 0, 0, 0);
             vLeft  = DirectX::XMVectorSet(-1, 0, 0, 0);
             vUp    = DirectX::XMVectorSet(0, 0, 1, 0); // Sky
             vDown  = DirectX::XMVectorSet(0, 0, -1, 0);
             vFront = DirectX::XMVectorSet(0, 1, 0, 0); // Horizon
             vBack  = DirectX::XMVectorSet(0, -1, 0, 0);
        } else {
             // Y-Up System (Standard DX/OpenGL/Unity)
             // Up is +Y
             // Front is +Z (LH) or -Z (RH)
             // Right is +X
             vRight = DirectX::XMVectorSet(1, 0, 0, 0);
             vLeft  = DirectX::XMVectorSet(-1, 0, 0, 0);
             vUp    = DirectX::XMVectorSet(0, 1, 0, 0);
             vDown  = DirectX::XMVectorSet(0, -1, 0, 0);
             if (isRH) {
                 vFront = DirectX::XMVectorSet(0, 0, -1, 0);
                 vBack  = DirectX::XMVectorSet(0, 0, 1, 0);
             } else {
                 vFront = DirectX::XMVectorSet(0, 0, 1, 0);
                 vBack  = DirectX::XMVectorSet(0, 0, -1, 0);
             }
        }

        DirectX::XMVECTOR targetDir;
        DirectX::XMVECTOR upDir = worldUp; // Default Up for the camera look-at

        switch (face) {
            case CubeFace::Right: targetDir = vRight; break;
            case CubeFace::Left:  targetDir = vLeft; break;
            case CubeFace::Up:    
                targetDir = vUp;
                // Fix Gimbal Lock: Looking Up (+Y or +Z), Up vector cannot be +Y/+Z.
                // Set Up vector to -Z (if Y-up) or -Y (if Z-up) -> Top of screen points "Back"
                // Or +Z/+Y -> Top of screen points "Front".
                // Standard Cubemap layout:
                // +Y Face (Top): Up is +Z. (Y-Up system)
                // Let's use vFront as Up reference.
                upDir = vFront;
                break;
            case CubeFace::Down:  
                targetDir = vDown;
                // Standard: Up is -Z.
                upDir = DirectX::XMVectorNegate(vFront);
                break;
            case CubeFace::Front: targetDir = vFront; break;
            case CubeFace::Back:  targetDir = vBack; break;
        }

        if (isRH) return DirectX::XMMatrixLookAtRH(eyePos, DirectX::XMVectorAdd(eyePos, targetDir), upDir);
        else      return DirectX::XMMatrixLookAtLH(eyePos, DirectX::XMVectorAdd(eyePos, targetDir), upDir);
    }

    bool CameraController::IsRightHandedProjection(const float* data) {
        if (data[11] < -0.9f) return true; // -1.0
        return false;
    }

    bool CameraController::IsProjectionMatrix(const float* data) {
        const float epsilon = 0.1f; // Relaxed from 0.05
        bool col3Zeros = (std::abs(data[3]) < epsilon && std::abs(data[7]) < epsilon && std::abs(data[15]) < epsilon);
        if (!col3Zeros) return false;
        
        float e11 = data[11];
        if (std::abs(e11 - 1.0f) < epsilon || std::abs(e11 + 1.0f) < epsilon) {
            return true;
        }
        return false;
    }

    bool CameraController::IsViewMatrix(const float* data, bool* outIsTransposed) {
        const float epsilon = 0.1f; // Relaxed

        // Row Major Candidate
        // [ x x x 0 ]
        // [ x x x 0 ]
        // [ x x x 0 ]
        // [ x x x 1 ]
        bool isRowMajor = (std::abs(data[3]) < epsilon && 
                           std::abs(data[7]) < epsilon && 
                           std::abs(data[11]) < epsilon && 
                           std::abs(data[15] - 1.0f) < epsilon);
                           
        // Transposed Candidate
        // [ x x x x ]
        // [ x x x x ]
        // [ x x x x ]
        // [ 0 0 0 1 ]
        bool isColMajor = (std::abs(data[12]) < epsilon && 
                           std::abs(data[13]) < epsilon && 
                           std::abs(data[14]) < epsilon && 
                           std::abs(data[15] - 1.0f) < epsilon);

        if (outIsTransposed) *outIsTransposed = isColMajor;

        // Pick the likely one
        if (isRowMajor) return true;
        if (isColMajor) return true;

        return false;
    }

    void CameraController::ForceReadBuffer(ID3D11Buffer* sourceBuffer) {
        if (!sourceBuffer) return;

        D3D11_BUFFER_DESC desc;
        sourceBuffer->GetDesc(&desc);
        
        if (desc.ByteWidth > 4096) return;
        if (desc.ByteWidth == 0) return;

        if (!m_readbackBuffer || desc.ByteWidth > 4096) { 
             if (!m_readbackBuffer) {
                 D3D11_BUFFER_DESC stageDesc = {};
                 stageDesc.ByteWidth = 4096; 
                 stageDesc.Usage = D3D11_USAGE_STAGING;
                 stageDesc.BindFlags = 0;
                 stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                 
                 ComPtr<ID3D11Device> device;
                 m_context->GetDevice(device.GetAddressOf());
                 
                 if (FAILED(device->CreateBuffer(&stageDesc, nullptr, m_readbackBuffer.GetAddressOf()))) {
                     LOG_ERROR("Failed to create readback buffer");
                     return;
                 }
             }
        }
        
        D3D11_BOX srcBox;
        srcBox.left = 0;
        srcBox.right = desc.ByteWidth;
        if (srcBox.right > 4096) srcBox.right = 4096;
        srcBox.top = 0;
        srcBox.bottom = 1;
        srcBox.front = 0;
        srcBox.back = 1;
        
        m_context->CopySubresourceRegion(m_readbackBuffer.Get(), 0, 0, 0, 0, sourceBuffer, 0, &srcBox);
        
        D3D11_MAPPED_SUBRESOURCE mapped;
        {
            ScopedHookIgnore ignore;
            if (SUCCEEDED(m_context->Map(m_readbackBuffer.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                auto& state = m_bufferCache[sourceBuffer];
                state.cpuData.resize(srcBox.right);
                memcpy(state.cpuData.data(), mapped.pData, srcBox.right);
                state.isDirty = true;
                m_context->Unmap(m_readbackBuffer.Get(), 0);
            } else {
                 LOG_ERROR("ForceRead Map Failed");
            }
        }
    }
}
