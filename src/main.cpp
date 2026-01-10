#include "pch.h"
#include <reshade.hpp>
#include "Core/Logger.h"
#include "Graphics/CubemapManager.h"

// Global Manager
static std::unique_ptr<Graphics::CubemapManager> g_CubemapManager;

static void on_init_device(reshade::api::device* device)
{
    Logger::Init();
    LOG_INFO("Init Device: ", (void*)device);
    // Initialize global resources if needed, though usually we wait for swapchain or present
}

static void on_destroy_device(reshade::api::device* device)
{
    LOG_INFO("Destroy Device: ", (void*)device);
    g_CubemapManager.reset();
    Logger::Shutdown();
}

static void on_init_swapchain(reshade::api::swapchain* swapchain, bool resize)
{
    LOG_INFO("Init Swapchain. Resize: ", resize);
    if (!g_CubemapManager) {
        g_CubemapManager = std::make_unique<Graphics::CubemapManager>(swapchain->get_device());
    }
}

static void on_destroy_swapchain(reshade::api::swapchain* /*swapchain*/, bool /*resize*/)
{
    LOG_INFO("Destroy Swapchain");
    // We can keep the manager alive during resize, or reset it.
    // If we reset, we lose recording state.
}

static void on_present(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect* /*source*/, const reshade::api::rect* /*dest*/, uint32_t /*dirty*/, const reshade::api::rect* /*dirty_rects*/)
{
    if (g_CubemapManager) {
        g_CubemapManager->OnPresent(queue, swapchain);
    }
}

static void on_draw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    if (g_CubemapManager) {
        g_CubemapManager->OnDraw(cmd_list, vertex_count, instance_count, first_vertex, first_instance);
    }
}

static void on_draw_indexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    if (g_CubemapManager) {
        g_CubemapManager->OnDrawIndexed(cmd_list, index_count, instance_count, first_index, vertex_offset, first_instance);
    }
}

static void on_update_buffer_region(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
{
    if (g_CubemapManager) {
        g_CubemapManager->OnUpdateBuffer(device, resource, data, size);
    }
}

static void on_map_buffer_region(reshade::api::device* device, reshade::api::resource resource, uint64_t /*offset*/, uint64_t size, reshade::api::map_access /*access*/, void** data)
{
    if (g_CubemapManager && data && *data) {
        g_CubemapManager->OnUpdateBuffer(device, resource, *data, size);
    }
}

static void on_bind_pipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline)
{
    if (g_CubemapManager) {
        g_CubemapManager->OnBindPipeline(cmd_list, stages, pipeline);
    }
}

// Addon Entry Point
extern "C" __declspec(dllexport) const char* reshade_addon_name = "WideCapture";
extern "C" __declspec(dllexport) const char* reshade_addon_description = "Captures 360 video from DX11 games.";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule))
            return FALSE;

        reshade::register_event<reshade::addon_event::init_device>(on_init_device);
        reshade::register_event<reshade::addon_event::destroy_device>(on_destroy_device);
        reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);
        reshade::register_event<reshade::addon_event::present>(on_present);

        // Capture Logic Events
        reshade::register_event<reshade::addon_event::draw>(on_draw);
        reshade::register_event<reshade::addon_event::draw_indexed>(on_draw_indexed);
        reshade::register_event<reshade::addon_event::update_buffer_region>(on_update_buffer_region);
        reshade::register_event<reshade::addon_event::map_buffer_region>(on_map_buffer_region);
        reshade::register_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline);

        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_addon(hModule);
        break;
    }
    return TRUE;
}
