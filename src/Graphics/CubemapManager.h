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
        void ExecuteCaptureCycle(IDXGISwapChain* swapChain);
        void OnResize();
        
        // Accessor needed for Hooks
        Camera::CameraController* GetCameraController();

        friend bool InitResources(CubemapManager* self, UINT w, UINT h);

    private:
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        bool m_isRecording = false; // Kept as member for fast access
        
        std::unique_ptr<CubemapManagerImpl> m_impl;
    };
}
