#include "graphics_settings.h"
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace wkc {
namespace {
using Member = unsigned GraphicsSettings::*;
constexpr std::array<Member, 11> members = {
    &GraphicsSettings::window_mode, &GraphicsSettings::output_resolution,
    &GraphicsSettings::vsync, &GraphicsSettings::anisotropy, &GraphicsSettings::frame_limit,
    &GraphicsSettings::world_distance, &GraphicsSettings::npc_distance,
    &GraphicsSettings::distance_fog, &GraphicsSettings::fps_counter,
    &GraphicsSettings::anti_aliasing, &GraphicsSettings::renderer
};
constexpr const char* keys[] = {"window_mode", "output_resolution", "vsync", "anisotropy",
    "frame_limit", "world_distance", "npc_distance", "distance_fog", "fps_counter", "anti_aliasing", "renderer"};
constexpr const char* labels[] = {"Window Mode", "Output Resolution", "VSync",
    "Anisotropic Filtering", "Frame Rate Limit", "World Distance",
    "NPC Distance", "Distance Fog", "FPS Counter", "Anti-Aliasing", "Graphics API"};
constexpr const char* modes[] = {"Windowed", "Borderless", "Fullscreen"};
constexpr const char* resolutions[] = {"720p", "1080p", "1440p", "2160p"};
constexpr const char* toggles[] = {"Off", "On"};
constexpr const char* filtering[] = {"Off", "2x", "4x", "8x", "16x"};
constexpr const char* rates[] = {"30", "40", "60"};
constexpr const char* aa[] = {"Off", "FXAA"};
constexpr const char* backends[] = {"Vulkan", "DirectX 12"};
constexpr const char* distances[] = {"Original", "1.5x", "2x"};
constexpr const char* fog[] = {"Original", "Reduced", "Off"};
struct Choices { const char* const* names; unsigned count; };
constexpr Choices choices[] = {{modes,3}, {resolutions,4}, {toggles,2}, {filtering,5},
    {rates,3}, {distances,3}, {distances,3}, {fog,3}, {toggles,2}, {aa,2}, {backends,2}};
std::mutex settings_mutex;
GraphicsSettings active;
unsigned active_renderer;
std::filesystem::path settings_path;
bool valid(GraphicsOption o) { return unsigned(o) < members.size(); }
}
unsigned graphics_choice_count(GraphicsOption o) { return valid(o) ? choices[unsigned(o)].count : 0; }
const char* graphics_option_label(GraphicsOption o) { return valid(o) ? labels[unsigned(o)] : ""; }
const char* graphics_choice_label(GraphicsOption o, unsigned v) {
    return valid(o) && v < graphics_choice_count(o) ? choices[unsigned(o)].names[v] : "";
}
unsigned graphics_value(const GraphicsSettings& s, GraphicsOption o) { return valid(o) ? s.*members[unsigned(o)] : 0; }
bool graphics_set(GraphicsSettings& s, GraphicsOption o, unsigned v) {
    if (!valid(o) || v >= graphics_choice_count(o)) return false;
    s.*members[unsigned(o)] = v; return true;
}
bool graphics_cycle(GraphicsSettings& s, GraphicsOption o, int direction) {
    const unsigned count = graphics_choice_count(o);
    if (!count || (direction != 1 && direction != -1)) return false;
    return graphics_set(s, o, (graphics_value(s,o) + (direction > 0 ? 1 : count-1)) % count);
}
unsigned graphics_frame_limit(const GraphicsSettings& s) {
    constexpr unsigned limits[] = {30,40,60};
    return s.frame_limit < 3 ? limits[s.frame_limit] : 30;
}
double graphics_vblank_period_ms(const GraphicsSettings& s) {
    const auto limit=graphics_frame_limit(s);
    return limit==30 ? 1000.0*1001.0/60000.0 : 500.0/limit;
}
std::string graphics_serialize(const GraphicsSettings& s) {
    std::string result = "version=1\n";
    for (unsigned i=0; i<members.size(); ++i)
        result += std::string(keys[i]) + "=" + std::to_string(s.*members[i]) + "\n";
    return result;
}
bool graphics_parse(const std::string& text, GraphicsSettings& result) {
    if (text.size() > 4096) return false;
    GraphicsSettings parsed;
    unsigned seen = 0; bool version = false; size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n',start); if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start,end-start); start = end+1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto equal = line.find('='); if (equal == std::string::npos) return false;
        unsigned value = 0; const char* first = line.data()+equal+1;
        const auto number = std::from_chars(first,line.data()+line.size(),value);
        if (number.ec != std::errc{} || number.ptr != line.data()+line.size()) return false;
        const auto key = line.substr(0,equal);
        if (key == "version") { if (version || value != 1) return false; version = true; continue; }
        unsigned i=0; while (i<members.size() && key != keys[i]) ++i;
        // 120, 144 and Unlocked (3-5) were removed: they read as 60.
        if (i == unsigned(GraphicsOption::frame_limit) && value >= 3 && value <= 5) value = 2;
        if (i == members.size() || (seen & (1u<<i)) || !graphics_set(parsed,GraphicsOption(i),value)) return false;
        seen |= 1u<<i;
    }
    if (!version) return false;
    result = parsed; return true;
}
bool graphics_load(const std::filesystem::path& path, GraphicsSettings& result) {
    std::ifstream f(path,std::ios::binary); if (!f) return false;
    std::string text(4097,'\0'); f.read(text.data(),text.size()); text.resize(size_t(f.gcount()));
    return graphics_parse(text,result);
}
bool graphics_save(const std::filesystem::path& path, const GraphicsSettings& settings) {
    for (unsigned i=0;i<members.size();++i)
        if (graphics_value(settings,GraphicsOption(i)) >= graphics_choice_count(GraphicsOption(i))) return false;
    auto temporary = path; temporary += ".tmp";
    { std::ofstream f(temporary,std::ios::binary|std::ios::trunc); if (!f) return false;
      f << graphics_serialize(settings); f.flush(); if (!f) return false; }
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error; std::filesystem::rename(temporary,path,error); return !error;
#endif
}
unsigned graphics_active_renderer() { std::lock_guard lock(settings_mutex); return active_renderer; }
GraphicsSettings graphics_snapshot() { std::lock_guard lock(settings_mutex); return active; }
void graphics_initialize(const std::filesystem::path& path) {
    std::lock_guard lock(settings_mutex); settings_path = path; active = {};
    graphics_load(path,active); active_renderer=active.renderer;
}
bool graphics_change(GraphicsOption o, int direction) {
    std::lock_guard lock(settings_mutex);
    auto candidate = active;
    if (!graphics_cycle(candidate,o,direction) || settings_path.empty() || !graphics_save(settings_path,candidate)) return false;
    active = candidate; return true;
}
}
