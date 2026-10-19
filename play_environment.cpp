#include "play_environment.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "play_environment_names.h"
#ifndef _WIN32
extern char** environ;
#endif

namespace {
bool allowed(const char* name) {
    static const char* names[]={
        "WKC2_PLAYER_MODE","WKC2_DISCARD_LOGS","WKC2_SKIP_INTRO",
        "WKC2_GUEST_PRESENT_ONLY","WKC2_FLIP_ON_VBLANK",
        "PS3_VFS_ROOT","PS3_TITLE","PS3_VERBOSE",
        "PS3RECOMP_FFMPEG_AVUTIL","PS3RECOMP_FFMPEG_AVCODEC",
        "PS3RECOMP_VK_GUEST_PROGRAMS","PS3RECOMP_VK_WINDOW","PS3RECOMP_RSX_ENGINE",
        "GCM_FLIPCB_ONTICK","GCM_SEMA_ACQUIRE","GCM_SUBCH1_2D",
        "RSX_ENGINE_ZTEX_GPU","RSX_VK_PRESENT_MODE"};
    for(auto candidate:names)if(!std::strcmp(candidate,name))return true;
    return false;
}
bool owned(const char* name) {
    // Covers future names in these namespaces as well as the generated list
    // of legacy/debug variables that have no common prefix.
    static const char* prefixes[]={"WKC_","WKC2_","SPU_","SPURS_","PPU_","RSX_",
        "GCM_","PS3_","PS3RECOMP_","PAD_","LD_","CELLMARK_","YDKJ_","YZ_"};
    for(auto prefix:prefixes)if(!std::strncmp(name,prefix,std::strlen(prefix)))return true;
    for(auto candidate:runtime_environment_names)if(!std::strcmp(candidate,name))return true;
    return false;
}
void set(const char* name,const char* value) {
#ifdef _WIN32
    _putenv_s(name,value?value:"");
#else
    if(value)setenv(name,value,1);else unsetenv(name);
#endif
}
std::vector<std::string> names() {
    std::vector<std::string> out;
#ifdef _WIN32
    auto environment=_environ;
#else
    auto environment=environ;
#endif
    if(environment)for(auto entry=environment;*entry;++entry) {
        const char* end=std::strchr(*entry,'=');
        if(end && end!=*entry) {
            out.emplace_back(*entry,static_cast<size_t>(end-*entry));
#ifdef _WIN32
            // Windows environment lookup is case-insensitive. A lower-case
            // diagnostic variable must not bypass the namespace/allowlist.
            for(char& ch:out.back())if(ch>='a' && ch<='z')ch=char(ch-'a'+'A');
#endif
        }
    }
    return out;
}
}
namespace wkc {
void prepare_play_environment() {
    const char* mode=std::getenv("WKC2_PLAYER_MODE");
    if(!mode || std::strcmp(mode,"1"))return;
    for(const auto& name:names())
        if(owned(name.c_str()) && !allowed(name.c_str()))set(name.c_str(),nullptr);
    set("WKC2_DISCARD_LOGS","1");set("PS3_VERBOSE","0");
}
bool play_environment_is_clean() {
    for(const auto& name:names())
        if(owned(name.c_str()) && !allowed(name.c_str()))return false;
    const char* quiet=std::getenv("WKC2_DISCARD_LOGS");
    const char* verbose=std::getenv("PS3_VERBOSE");
    return quiet && !std::strcmp(quiet,"1") && verbose && !std::strcmp(verbose,"0");
}
}
