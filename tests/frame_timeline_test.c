#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ps3emu/wkc_frame_trace.h"
#include <assert.h>

static DWORD WINAPI publish(LPVOID unused)
{
    (void)unused;
    for (unsigned i = 0; i < 10000; i++) {
        uint64_t start = wt_begin();
        assert(start);
        wt_end(WT_SPU_RUN, 19, i, start);
    }
    return 0;
}

int main(int argc, char** argv)
{
    assert(argc == 3 && wt_begin() == 0);
    assert(_putenv_s("WKC_TRACE_GATE", argv[1]) == 0);
    assert(_putenv_s("WKC_TRACE_OUT", argv[2]) == 0);
    FILE* file = fopen(argv[1], "wb");
    assert(file);
    fclose(file);
    wt_tick();
    assert(wt_begin());
    HANDLE threads[8];
    for (int i = 0; i < 8; i++) {
        threads[i] = CreateThread(NULL, 0, publish, NULL, 0, NULL);
        assert(threads[i]);
    }
    assert(WaitForMultipleObjects(8, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (int i = 0; i < 8; i++) CloseHandle(threads[i]);
    assert(g_wkc_private_trace.n == 80000 && !g_wkc_private_trace.dropped);
    for (unsigned i = 0; i < 80000; i++) {
        const wt_record* record = &g_wkc_private_trace.records[i];
        assert(record->kind == WT_SPU_RUN && record->a == 19 && record->b < 10000);
        assert(record->tid && record->t1 >= record->t0);
    }
    g_wkc_private_trace.end = wt_now() - 1;
    wt_tick();
    assert(!wt_begin() && g_wkc_private_trace.on == 2 && !g_wkc_private_trace.writers);
    wt_end(WT_SPU_RUN, 0, 0, 0);
    assert(g_wkc_private_trace.n == 80000);
    file = fopen(argv[2], "rb");
    assert(file);
    unsigned lines = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF) lines += ch == '\n';
    fclose(file);
    assert(lines == 80001);
    remove(argv[1]);
    remove(argv[2]);
    puts("frame timeline: 80000 complete records from eight writers; disabled/dump checks pass");
    return 0;
}
