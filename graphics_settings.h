#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace wkc {
enum class GraphicsOption : unsigned {
    window_mode, output_resolution, vsync, anisotropy, frame_limit,
    world_distance, npc_distance, distance_fog, fps_counter, anti_aliasing, renderer, count
};
struct GraphicsSettings {
    unsigned window_mode = 0;       // windowed, borderless, fullscreen
    unsigned output_resolution = 0; // 720p, 1080p, 1440p, 2160p
    unsigned vsync = 1;
    unsigned anisotropy = 0;        // off, 2x, 4x, 8x, 16x
    unsigned frame_limit = 0;       // 30, 40, 60
    unsigned world_distance = 0;    // original, 1.5x, 2x
    unsigned npc_distance = 0;
    unsigned distance_fog = 0;      // original, reduced, off
    unsigned fps_counter = 0;
    unsigned anti_aliasing = 0;     // off, FXAA
    unsigned renderer = 0;          // Vulkan, DirectX 12 (restart required)
    bool operator==(const GraphicsSettings&) const = default;
};
unsigned graphics_choice_count(GraphicsOption option);
const char* graphics_option_label(GraphicsOption option);
const char* graphics_choice_label(GraphicsOption option, unsigned value);
unsigned graphics_value(const GraphicsSettings& settings, GraphicsOption option);
bool graphics_set(GraphicsSettings& settings, GraphicsOption option, unsigned value);
bool graphics_cycle(GraphicsSettings& settings, GraphicsOption option, int direction);
unsigned graphics_frame_limit(const GraphicsSettings& settings);
// Game time continues to use the wall clock.
double graphics_vblank_period_ms(const GraphicsSettings& settings);
std::string graphics_serialize(const GraphicsSettings& settings);
bool graphics_parse(const std::string& text, GraphicsSettings& settings);
bool graphics_load(const std::filesystem::path& path, GraphicsSettings& settings);
bool graphics_save(const std::filesystem::path& path, const GraphicsSettings& settings);
GraphicsSettings graphics_snapshot();
unsigned graphics_active_renderer();
void graphics_initialize(const std::filesystem::path& path);
bool graphics_change(GraphicsOption option, int direction);
}
