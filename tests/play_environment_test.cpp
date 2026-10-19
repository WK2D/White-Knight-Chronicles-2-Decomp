#include "play_environment.h"
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
int wkc2_test_play_environment() {
    if(!wkc::play_environment_is_clean())return 1;
    const char* media=std::getenv("PS3RECOMP_FFMPEG_AVUTIL");
    if(!media || std::strcmp(media,"clean-play-media-sentinel"))return 2;
    const char* path=std::getenv("PATH");if(!path || !*path)return 3;
#ifdef _WIN32
    // Check the Win32 environment too, not just the CRT's cached getenv table.
    for(auto name:{"WKC2_SAMPLE_PROFILE","SPU_INTERP_PROFILE","RSX_ENGINE_SNAP_DIR",
                   "LD_FRAME_DUMP","PAD_FILE","WKC_NATIVE_SKINNING_TRACE"}) {
        char value[2];if(GetEnvironmentVariableA(name,value,sizeof value))return 4;
    }
#endif
    std::puts("clean-play environment verified");return 0;
}
