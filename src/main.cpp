#include "pch.h"
#include "Core/Hooks.h"
#include "Core/Logger.h"

// Global atomic flag for thread cycle control
std::atomic<bool> g_IsRunning{ true };

// Global Hooks instance to ensure lifetime control
static std::unique_ptr<Core::Hooks> g_Hooks;

void CaptureThread(HMODULE hModule) {
    Logger::Init();
    LOG_INFO("WideCapture injected successfully. Thread ID: ", GetCurrentThreadId());

    try {
        // Wait for game initialization
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Init Hooks
        g_Hooks = std::make_unique<Core::Hooks>();
        if (!g_Hooks->Install()) {
            LOG_ERROR("Failed to install hooks. Aborting.");
            g_IsRunning = false;
        } else {
            LOG_INFO("Hooks installed. Press END to unload.");
        }

        // Main Loop
        while (g_IsRunning) {
            if (GetAsyncKeyState(VK_END) & 0x8000) {
                g_IsRunning = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (g_Hooks) {
            g_Hooks->Uninstall();
            g_Hooks.reset();
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Critical error in CaptureThread: ", e.what());
    }

    LOG_INFO("Unloading library...");
    Logger::Shutdown();
    FreeLibraryAndExitThread(hModule, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        MessageBoxA(NULL, "Full DLL Attached! Testing...", "WideCapture Debug", MB_OK);
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)CaptureThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        g_IsRunning = false;
        break;
    }
    return TRUE;
}
