#ifdef NDEBUG
#undef NDEBUG
#endif
#include "graphics_settings.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
using namespace wkc;
int main() {
    assert(std::string(wkc::graphics_choice_label(wkc::GraphicsOption::anisotropy,0))=="Off");
    assert(std::string(wkc::graphics_choice_label(wkc::GraphicsOption::anisotropy,4))=="16x");

    const GraphicsSettings original;
    assert(graphics_frame_limit(original)==30 && original.vsync==1);
    for (unsigned i=0;i<unsigned(GraphicsOption::count);++i) {
        auto s=original; const auto o=GraphicsOption(i); const auto n=graphics_choice_count(o);
        const auto initial=graphics_value(s,o);
        for (unsigned j=0;j<n;++j) {
            assert(graphics_cycle(s,o,1)); GraphicsSettings decoded;
            assert(graphics_parse(graphics_serialize(s),decoded) && s==decoded);
            assert(std::string(graphics_choice_label(o,graphics_value(s,o))).size());
        }
        assert(graphics_value(s,o)==initial);
        assert(graphics_cycle(s,o,-1)); assert(graphics_cycle(s,o,1) && s==original);
        assert(!graphics_set(s,o,n) && s==original);
    }
    auto s=original;
    constexpr unsigned expected[]={30,40,60,120,144,0};
    for(unsigned i=0;i<6;++i) {
        assert(graphics_set(s,GraphicsOption::frame_limit,i)); assert(graphics_frame_limit(s)==expected[i]);
        const double period=graphics_vblank_period_ms(s);
        if(i==0) assert(period>16.68 && period<16.69);
        else if(i==5) assert(period==0.0);
        else assert(period*expected[i]>499.99 && period*expected[i]<500.01);
    }
    assert(graphics_parse("version=1\nframe_limit=2\n",s) && s.anti_aliasing==0 && s.renderer==0);
    for(const char* bad : {"", "version=2\n", "version=1\nframe_limit=6\n", "version=1\nvsync=-1\n",
        "version=1\nvsync=0\nvsync=1\n", "version=1\nother=0\n", "version=1\nvsync=1x\n"}) {
        const auto before=s; assert(!graphics_parse(bad,s) && s==before);
    }
    const auto path=std::filesystem::temp_directory_path()/
        ("wkc-graphics-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ini");
    assert(graphics_save(path,s)); GraphicsSettings loaded;
    assert(graphics_load(path,loaded) && loaded==s);
    graphics_initialize(path); assert(graphics_snapshot()==s);
    assert(graphics_change(GraphicsOption::vsync,1));
    assert(graphics_load(path,loaded) && loaded==graphics_snapshot());
    { std::ofstream f(path); f<<"version=1\nframe_limit=999\n"; }
    graphics_initialize(path); assert(graphics_snapshot()==original);
    std::filesystem::remove(path);
    return 0;
}
