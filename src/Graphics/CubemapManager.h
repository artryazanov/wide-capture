#include <wrl/client.h>
#include <d3d11.h>
#include <memory>

// Forward declarations
namespace Camera { class CameraController; }

namespace Graphics {
    using Microsoft::WRL::ComPtr;

    // Forward declaration of the implementation struct
    struct CubemapManagerImpl;

    class CubemapManager {
    public:
        CubemapManager(ID3D11Device* device, ID3D11DeviceContext* context);
        ~CubemapManager();

        bool IsRecording() const;

        // Return true if the frame should be presented, false if it should be skipped
        bool PresentHook(IDXGISwapChain* swapChain);
        
        // Accessor needed for Hooks
        Camera::CameraController* GetCameraController();

        friend bool InitResources(CubemapManager* self, UINT w, UINT h, DXGI_FORMAT format);
        void OnResize();

    private:
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        bool m_isRecording = false;
        int m_frameCycle = 6; // Start at 6 so first increment makes it 0 (Wait, 6 % 7 = 6 -> Player)
        
        std::unique_ptr<CubemapManagerImpl> m_impl;

        // Helper to process the full cycle
        void ProcessCycle();
        // Helper to capture a single face
        void CaptureFace(int faceIndex, ID3D11Texture2D* backBuffer);
    };
}
