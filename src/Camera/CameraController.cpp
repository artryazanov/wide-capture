#include "pch.h"
#include "CameraController.h"
#include "MatrixMath.h"
#include "../Core/Logger.h"

namespace Camera {
    CameraController::CameraController(ID3D11DeviceContext* context) : m_context(context) {}

    void CameraController::Reset() {
        m_bufferCache.clear();
    }

    void CameraController::SetTargetFace(CubeFace face) {
        m_currentFace = face;
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

        static int usLog = 0;
        if (usLog < 10) { LOG_INFO("OnUpdateSubresource: ", resource); usLog++; }

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

         static int mapLog = 0;
         if (mapLog < 10) { LOG_INFO("OnMap: ", resource); mapLog++; }

         m_bufferCache[resource].mappedPtr = mapped->pData;
    }

    void CameraController::OnUnmap(ID3D11DeviceContext* pContext, ID3D11Resource* resource) {
         // Reverted to empty/safe for stability.
    }

    // --- logic ---

    ID3D11Buffer* CameraController::CheckAndGetReplacementBuffer(ID3D11DeviceContext* context, ID3D11Buffer* originalBuffer) {
        if (!originalBuffer) return nullptr;
        
        std::lock_guard<std::mutex> lock(m_cacheMutex);

        auto it = m_bufferCache.find(originalBuffer);
        
        if (it == m_bufferCache.end() || it->second.cpuData.empty()) {
             // CACHE MISS: Force Read from GPU
             ForceReadBuffer(originalBuffer);
             
             // Re-check
             it = m_bufferCache.find(originalBuffer);
             if (it == m_bufferCache.end() || it->second.cpuData.empty()) {
                 return nullptr;
             }
        }

        // 'it' is valid here
        auto& state = it->second;
        const float* data = (const float*)state.cpuData.data();
        size_t floatCount = state.cpuData.size() / sizeof(float);
        
        if (floatCount < 16) return nullptr;

        static int debugLogLimit = 0;
        if (debugLogLimit < 0) { // Disabled
            // Log first 16 floats of the first few buffers to see what we are dealing with
            std::string vals = "";
            for(int k=0; k<16 && k<floatCount; ++k) vals += std::to_string(data[k]) + " ";
            LOG_INFO("Buf inspect (", originalBuffer, "): ", vals);
            debugLogLimit++;
        }

        // 2. Detection (Heuristic)
        // Check stored flag first
        bool isCamera = state.isCamera;
        
        if (!isCamera) {
            // Heuristic Check
            for (size_t i = 0; i <= floatCount - 16; i += 4) { 
                 if (IsProjectionMatrix(data + i)) {
                      state.isCamera = true;
                      isCamera = true;
                      
                      static std::unordered_map<ID3D11Buffer*, bool> loggedBufs;
                      if (!loggedBufs[originalBuffer]) {
                          LOG_INFO("Camera Buffer (Proj) Detected! Ptr: ", originalBuffer, " Size: ", state.cpuData.size(), " Offset: ", i);
                          loggedBufs[originalBuffer] = true;
                      }
                      break;
                 }
                 
                 // Also check for View Matrix to detect buffers that ONLY have View
                 if (IsViewMatrix(data + i, nullptr)) {
                      state.isCamera = true;
                      isCamera = true;
                      
                      static std::unordered_map<ID3D11Buffer*, bool> loggedBufsView;
                      if (!loggedBufsView[originalBuffer]) {
                          LOG_INFO("Camera Buffer (View) Detected! Ptr: ", originalBuffer, " Size: ", state.cpuData.size(), " Offset: ", i);
                          loggedBufsView[originalBuffer] = true;
                      }
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
        
        auto& state = m_bufferCache[originalBuffer];
        size_t size = state.cpuData.size();
        
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
                 size_t floatCount = size / sizeof(float);
                 
                  // Scan for ANY Projection or View matrices
                  for (size_t i = 0; i <= floatCount - 16; i += 4) {
                      float* searchPtr = outData + i;
                      
                      if (IsProjectionMatrix((const float*)(originalData + i))) {
                          // Found Projection
                          bool isRH = IsRightHandedProjection((const float*)(originalData + i));
                          m_isRH = isRH; // Cache for View Matrix

                          // Log for verification
                          static bool loggedProj = false;
                          if (!loggedProj) {
                              const float* p = (const float*)(originalData + i);
                              LOG_INFO("Found Projection Matrix! RH=", isRH, " [10]=", p[10], " [11]=", p[11]);
                              loggedProj = true;
                          }

                          DirectX::XMMATRIX projMat = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)(originalData + i));
                          // We use the cached m_isRH inside the helper or just call the right one here.
                          // Let's call the right one here for clarity.
                          DirectX::XMMATRIX newProj;
                          if (isRH) {
                               newProj = DirectX::XMMatrixPerspectiveFovRH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
                          } else {
                               newProj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
                          }
                          DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)searchPtr, newProj);
                      }
                      
                      bool isTransposed = false;
                      if (IsViewMatrix((const float*)(originalData + i), &isTransposed)) {
                           // Found View -> Replace with Face View
                           DirectX::XMMATRIX viewMat = DirectX::XMLoadFloat4x4((const DirectX::XMFLOAT4X4*)(originalData + i));
                           
                           if (isTransposed) {
                               viewMat = DirectX::XMMatrixTranspose(viewMat);
                           }

                           // Log Camera Position extraction once per detected buffer/face
                           static int viewLog = 0;
                           if (viewLog < 20) {
                               DirectX::XMVECTOR det;
                               DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, viewMat);
                               DirectX::XMFLOAT4 eyePos;
                               DirectX::XMStoreFloat4(&eyePos, invView.r[3]);
                               
                               LOG_INFO("ViewMat Detected! Offset=", i, " Transposed=", isTransposed, 
                                        " Eye=(", eyePos.x, ",", eyePos.y, ",", eyePos.z, ") Face=", (int)m_currentFace);
                               viewLog++;
                           }

                           DirectX::XMMATRIX newView = GetViewMatrixForFace(viewMat, m_currentFace, m_isRH);
                           
                           if (isTransposed) {
                               newView = DirectX::XMMatrixTranspose(newView);
                           }
                           
                           DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)searchPtr, newView);
                       }
                   }

                 m_context->Unmap(state.replacementBuffer.Get(), 0);
            }
        }
        
        return state.replacementBuffer.Get();
    }
    
    DirectX::XMMATRIX CameraController::GetViewMatrixForFace(DirectX::XMMATRIX originalView, CubeFace face, bool isRH) {
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, originalView);
        DirectX::XMVECTOR eyePos = invView.r[3]; 

        // World Up vector
        DirectX::XMVECTOR up = DirectX::XMVectorSet(0, 1, 0, 0);
        DirectX::XMVECTOR forward;

        // Basis Vectors
        DirectX::XMVECTOR vRight = DirectX::XMVectorSet(1, 0, 0, 0);
        DirectX::XMVECTOR vLeft  = DirectX::XMVectorSet(-1, 0, 0, 0);
        DirectX::XMVECTOR vUp    = DirectX::XMVectorSet(0, 1, 0, 0);
        DirectX::XMVECTOR vDown  = DirectX::XMVectorSet(0, -1, 0, 0);
        DirectX::XMVECTOR vFront = DirectX::XMVectorSet(0, 0, 1, 0);
        DirectX::XMVECTOR vBack  = DirectX::XMVectorSet(0, 0, -1, 0);

        if (isRH) {
             // In RH: Forward is -Z, Right is +X, Up is +Y
             vFront = DirectX::XMVectorSet(0, 0, -1, 0);
             vBack  = DirectX::XMVectorSet(0, 0, 1, 0);
        }

        switch (face) {
            case CubeFace::Front: forward = vFront; break;
            case CubeFace::Back:  forward = vBack; break;
            case CubeFace::Left:  forward = vLeft; break;
            case CubeFace::Right: forward = vRight; break;
            case CubeFace::Up:    
                forward = vUp; 
                // To avoid Gimbal Lock, change Up vector
                // If looking UP (+Y), Standard Up is usually +Z (or -Z) for top face orientation.
                // In cubemaps: Up Face (+Y) usually has "Up" vector as -Z (RH) or +Z?
                // Standard DX Cubemap: Up Face -> +Y, Up Vector -> +Z (Wait, standard is -Z for Up face?)
                // Let's stick effectively to:
                up = vFront; // Look UP, Top of screen is 'Front' relative to world
                if (isRH) up = DirectX::XMVectorSet(0, 0, -1, 0); // Actually usually -Z for RH Up face 
                else      up = DirectX::XMVectorSet(0, 0, 1, 0); // LH
                break;
            case CubeFace::Down:  
                forward = vDown; 
                if (isRH) up = DirectX::XMVectorSet(0, 0, 1, 0);
                else      up = DirectX::XMVectorSet(0, 0, -1, 0);
                break;
        }

        if (isRH) return DirectX::XMMatrixLookAtRH(eyePos, DirectX::XMVectorAdd(eyePos, forward), up);
        else      return DirectX::XMMatrixLookAtLH(eyePos, DirectX::XMVectorAdd(eyePos, forward), up);
    }

    bool CameraController::IsRightHandedProjection(const float* data) {
        // In standard projection:
        // LH: [2][2] and [3][2] depend on n, f. [2][3] is +1.
        // RH: [2][3] is -1.
        // Index 11 is [2][3] (row-major 0-based: 0..3, 4..7, 8..11).
        
        // Wait, IsProjectionMatrix checked data[11] - 1.0f < epsilon.
        // If data[11] is 1.0, it's typically LH (z mapped to w).
        // If data[11] is -1.0, it's typically RH (z mapped to -w for clip space).
        
        // Let's check data[11].
        if (data[11] < -0.9f) return true; // -1.0
        return false;
    }

    DirectX::XMMATRIX CameraController::GetProjectionMatrix90FOV(DirectX::XMMATRIX originalProj) {
        return DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
    }
    
    // Updated IsProjectionMatrix to allow -1.0 at index 11 for RH support
    bool CameraController::IsProjectionMatrix(const float* data) {
        const float epsilon = 0.05f; 
        bool col3Zeros = (std::abs(data[3]) < epsilon && std::abs(data[7]) < epsilon && std::abs(data[15]) < epsilon);
        if (!col3Zeros) return false;
        
        // Check element 11 ( [2][3] )
        float e11 = data[11];
        if (std::abs(e11 - 1.0f) < epsilon || std::abs(e11 + 1.0f) < epsilon) {
            return true;
        }
        return false;
    }



    bool CameraController::IsViewMatrix(const float* data, bool* outIsTransposed) {
        // View Matrix Heuristic (Row Major standard)
        // [ R R R 0 ]
        // [ R R R 0 ]
        // [ R R R 0 ]
        // [ T T T 1 ]
        // But in memory, it could be transposed.
        // Standard HLSL cbuffer packing matches float4x4. 
        // DirectXMath stores row-major.

        const float epsilon = 0.05f;

        // Check for 0,0,0,1 column (usually index 3, 7, 11, 15)
        bool isRowMajor = (std::abs(data[3]) < epsilon && 
                           std::abs(data[7]) < epsilon && 
                           std::abs(data[11]) < epsilon && 
                           std::abs(data[15] - 1.0f) < epsilon);
                           
        // Check for 0,0,0,1 row (usually index 12, 13, 14, 15 from transposed)
        // [ R R R T ]
        // [ R R R T ]
        // [ R R R T ]
        // [ 0 0 0 1 ]
        bool isColMajor = (std::abs(data[12]) < epsilon && 
                           std::abs(data[13]) < epsilon && 
                           std::abs(data[14]) < epsilon && 
                           std::abs(data[15] - 1.0f) < epsilon);

        if (!isRowMajor && !isColMajor) return false;

        // Filter out Identity (not interesting to replace if it's just identity, though technically valid)
        // But maybe the game uses Identity for View at origin? 
        // Identity has 1 at 0, 5, 10, 15.
        // Let's filter slightly: Sum of absolute values should be > 1.0 (Identity is 4.0).
        
        // Filter out Projection (which also has 1 at 15 sometimes, but we catch specific proj params above)
        // IsProjectionMatrix checks for [11] == -1 or 1, and [15] == 0.
        // View Matrix has [15] == 1.
        
        return true;
    }
    void CameraController::ForceReadBuffer(ID3D11Buffer* sourceBuffer) {
        // Called from CheckAndGetReplacementBuffer which holds lock.
        if (!sourceBuffer) return;

        // 1. Get Desc to know size
        D3D11_BUFFER_DESC desc;
        sourceBuffer->GetDesc(&desc);
        
        if (desc.ByteWidth > 4096) return; // Too big for camera
        if (desc.ByteWidth == 0) return;

        // 2. Create Staging Buffer if needed
        if (!m_readbackBuffer || desc.ByteWidth > 4096) { 
             // Just create max size once
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
        
        // 3. Copy
        // We can use CopySubresourceRegion. 
        // Note: CopyResource requires same size/type. CopySubresourceRegion is often safer if regions match or for partial.
        // But for Staging we usuall use CopySubresourceRegion.
        
        // Clamp copy size to staging buffer size
        D3D11_BOX srcBox;
        srcBox.left = 0;
        srcBox.right = desc.ByteWidth;
        if (srcBox.right > 4096) srcBox.right = 4096;
        srcBox.top = 0;
        srcBox.bottom = 1;
        srcBox.front = 0;
        srcBox.back = 1;
        
        m_context->CopySubresourceRegion(m_readbackBuffer.Get(), 0, 0, 0, 0, sourceBuffer, 0, &srcBox);
        
        // 4. Map and Read
        D3D11_MAPPED_SUBRESOURCE mapped;
        {
            ScopedHookIgnore ignore; // Do NOT trigger local Hook_Map / Hook_Unmap callbacks
            if (SUCCEEDED(m_context->Map(m_readbackBuffer.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                auto& state = m_bufferCache[sourceBuffer];
                state.cpuData.resize(srcBox.right);
                memcpy(state.cpuData.data(), mapped.pData, srcBox.right);
                state.isDirty = true;
                
                m_context->Unmap(m_readbackBuffer.Get(), 0);
                
                // LOG_INFO("ForceRead Success! Size: ", srcBox.right, " Ptr: ", sourceBuffer);
            } else {
                 LOG_ERROR("ForceRead Map Failed");
            }
        }
    }
}
