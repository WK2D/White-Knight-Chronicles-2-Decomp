/* Exercise the production recorder itself without creating a GPU/window. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "rsx_present_metrics.h"
typedef uint32_t u32;
typedef uint64_t u64;
static SRWLOCK s_ld_metrics_lock = SRWLOCK_INIT;
static rsx_present_metrics s_ld_present_metrics;
#include "present_measure_fixture.h"

static void check(const char* player, const char* measure, int expected) {
    char path[256];
    snprintf(path,sizeof(path),"scratch\\present_qpc_%lu.csv",GetCurrentProcessId());
    DeleteFileA(path);
    _putenv_s("WKC2_PLAYER_MODE",player);
    _putenv_s("RSX_PRESENT_MEASURE",measure);
    ld_present_measure_init();
    ld_present_measure_record(1);
    assert(s_ld_present_metrics.started); /* FPS overlay remains functional */
    assert(g_ld_present_total == (u64)expected);
    ld_present_measure_dump();
    assert((GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)==expected);
    if(expected)assert(DeleteFileA(path));
}
int main(void) {
    CreateDirectoryA("scratch",NULL);
    _putenv_s("RSX_SCHEDULE_DIAG","");
    check("","",0);check("","0",0);check("","1",1);
    check("1","1",0);check("1","",0);
    _putenv_s("RSX_SCHEDULE_DIAG","1");
    check("","",0);check("1","1",0);check("","1",1);
    puts("Production presentation recorder: eight opt-in/player/CSV cases passed");
    return 0;
}
