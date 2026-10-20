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
    const char* native_job=std::getenv("WKC_NATIVE_GEOMETRY_JOB");
    if(!native_job || (std::strcmp(native_job,"0") && std::strcmp(native_job,"1")))return 5;
#ifdef _WIN32
    // Check the Win32 environment too, not just the CRT's cached getenv table.
    for(auto name:{"WKC2_SAMPLE_PROFILE","SPU_INTERP_PROFILE","RSX_ENGINE_SNAP_DIR",
                   "LD_FRAME_DUMP","PAD_FILE","WKC_NATIVE_SKINNING_TRACE",
                   "WKC_NATIVE_SKIN_STREAM","WKC_NATIVE_SKIN_STREAM_VERIFY",
                   "WKC_NATIVE_GEOMETRY_DIAGNOSTICS","WKC_NATIVE_GEOMETRY_PARALLEL_PREPARE"}) {
        char value[2];if(GetEnvironmentVariableA(name,value,sizeof value))return 4;
    }
#endif
    std::printf("clean-play environment verified; native-job-mode=%s\n",native_job);return 0;
}
