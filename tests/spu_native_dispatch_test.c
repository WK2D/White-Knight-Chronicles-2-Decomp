/* Exercise the production dispatcher with synthetic image and job boundaries. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#if defined(_MSC_VER) && !defined(__clang__)
#define __builtin_expect(value, expected) (value)
#endif
#include "spu_workload.h"

#if defined(_MSC_VER)
#define FIXTURE_TLS __declspec(thread)
#else
#define FIXTURE_TLS __thread
#endif
#define SPU_WORKLOAD_DIAG_TLS FIXTURE_TLS

typedef struct fixture_job {
    const uint8_t* image;
    uint32_t image_size, job_ea, descriptor_size;
    int image_id, native_result, runner_result;
    spu_lifted_entry_fn entry;
    unsigned fingerprints, begins, hooks, runners, ends;
    unsigned expected_slot, published_slot;
    int scoped, null_token, publish;
    char order[8];
    unsigned order_size;
    LONG end_sequence, execution_sequence;
    HANDLE attempted, acquired, executing, release;
    int block_hook, block_runner, dispatch_result;
} fixture_job;

static FIXTURE_TLS fixture_job* current_job;
static CRITICAL_SECTION slot_lock;
static fixture_job* slot_owner;
static unsigned slot_value;
static volatile LONG sequence;
static const uint8_t image_a[] = {0x19, 0x4a, 0x76};
static const uint8_t image_b[] = {0x23, 0x84, 0x19, 0xb7};
static const uint8_t image_miss[] = {0x70};
static const uint8_t image_no_entry[] = {0x71};

static void entry_a(spu_context* context) { (void)context; assert(!"direct lifted entry"); }
static void entry_b(spu_context* context) { (void)context; assert(!"direct lifted entry"); }

/* Synthetic lookup storage; all callback types and results come from the SDK. */
static struct {
    uint64_t fp;
    spu_lifted_entry_fn fn;
    int image_id;
} s_registry[] = {{0x19, entry_a, 19}, {0x23, entry_b, 23}, {0x71, NULL, 71}};
static unsigned s_registry_count = sizeof s_registry / sizeof s_registry[0];
spu_native_job_hook_fn g_spu_native_job_hook;
spu_job_scope_begin_fn g_spu_job_scope_begin;
spu_job_scope_end_fn g_spu_job_scope_end;

static void record_event(char event)
{
    fixture_job* job = current_job;
    assert(job && job->order_size + 1 < sizeof job->order);
    job->order[job->order_size++] = event;
    job->order[job->order_size] = '\0';
}

static fixture_job* check_arguments(int image_id, uint32_t ea, uint32_t size)
{
    fixture_job* job = current_job;
    assert(job && image_id == job->image_id);
    assert(ea == job->job_ea && size == job->descriptor_size);
    return job;
}

static void assert_scope(fixture_job* job)
{
    if (job->scoped) assert(slot_owner == job && job->begins == 1 && !job->ends);
    else assert(!slot_owner && !job->begins && !job->ends);
}

static void publish_result(fixture_job* job)
{
    if (job->publish) {
        assert_scope(job);
        assert(slot_value == job->expected_slot);
        slot_value = job->published_slot;
    }
}

uint64_t spu_workload_fingerprint(const void* bytes, size_t size)
{
    fixture_job* job = current_job;
    assert(job && bytes == job->image && size == job->image_size && size);
    ++job->fingerprints;
    return *(const uint8_t*)bytes;
}

static int ps3_log_verbose(void) { return 0; }
static char* fixture_getenv(const char* name)
{
    /* Keep diagnostic environment variables from creating files or changing
     * the test's logging; the extracted dispatch body is otherwise untouched. */
    assert(!strcmp(name, "SPU_DUMP_MISS") || !strcmp(name, "SPURS_JOB_TRACE_FP"));
    return NULL;
}

static void* begin_scope(int image_id, uint32_t ea, uint32_t size)
{
    fixture_job* job = check_arguments(image_id, ea, size);
    assert(job->scoped && !job->begins);
    ++job->begins;
    if (job->attempted) assert(SetEvent(job->attempted));
    EnterCriticalSection(&slot_lock);
    assert(!slot_owner);
    slot_owner = job;
    if (job->publish) assert(slot_value == job->expected_slot);
    record_event('B');
    if (job->acquired) assert(SetEvent(job->acquired));
    return job->null_token ? NULL : job;
}

static void end_scope(int image_id, uint32_t ea, uint32_t size, void* token)
{
    fixture_job* job = check_arguments(image_id, ea, size);
    assert_scope(job);
    assert(token == (job->null_token ? NULL : job));
    assert(job->hooks + job->runners > 0 && !job->ends);
    if (job->publish) assert(slot_value == job->published_slot);
    ++job->ends;
    record_event('E');
    job->end_sequence = InterlockedIncrement(&sequence);
    slot_owner = NULL;
    LeaveCriticalSection(&slot_lock);
}

static void hold_execution(fixture_job* job, int hold)
{
    job->execution_sequence = InterlockedIncrement(&sequence);
    if (job->executing) assert(SetEvent(job->executing));
    if (hold) assert(WaitForSingleObject(job->release, 5000) == WAIT_OBJECT_0);
    assert_scope(job);
}

static int native_hook(int image_id, uint32_t ea, uint32_t size)
{
    fixture_job* job = check_arguments(image_id, ea, size);
    assert_scope(job);
    assert(!job->hooks && !job->runners);
    ++job->hooks;
    record_event('N');
    if (job->native_result != SPU_NATIVE_JOB_DECLINED)
        hold_execution(job, job->block_hook);
    if (job->native_result == SPU_NATIVE_JOB_COMPLETED) publish_result(job);
    return job->native_result;
}

int spu_run_spurs_job(spu_lifted_entry_fn entry, int image_id, uint32_t ea, uint32_t size)
{
    fixture_job* job = check_arguments(image_id, ea, size);
    assert(entry == job->entry);
    assert_scope(job);
    assert(!job->runners);
    if (job->hooks) assert(job->native_result == SPU_NATIVE_JOB_DECLINED);
    ++job->runners;
    record_event('R');
    hold_execution(job, job->block_runner);
    if (job->runner_result >= 0) publish_result(job);
    return job->runner_result;
}

#define getenv fixture_getenv
#include "spu_native_dispatch_fixture.inc"
#undef getenv

static fixture_job make_job(int second_image)
{
    fixture_job job = {0};
    job.image = second_image ? image_b : image_a;
    job.image_size = second_image ? sizeof image_b : sizeof image_a;
    job.image_id = second_image ? 23 : 19;
    job.entry = second_image ? entry_b : entry_a;
    job.job_ea = second_image ? 0x87654321u : 0x12345678u;
    job.descriptor_size = second_image ? 512 : 256;
    return job;
}

static int dispatch(fixture_job* job)
{
    assert(!current_job);
    current_job = job;
    int result = spu_workload_dispatch_job(job->image, job->image_size,
                                          job->job_ea, job->descriptor_size);
    current_job = NULL;
    return result;
}

static void install_callbacks(unsigned mask)
{
    g_spu_native_job_hook = (mask & 4) ? native_hook : NULL;
    g_spu_job_scope_begin = (mask & 2) ? begin_scope : NULL;
    g_spu_job_scope_end = (mask & 1) ? end_scope : NULL;
}

static unsigned native_cases(void)
{
    const int results[] = {SPU_NATIVE_JOB_COMPLETED, SPU_NATIVE_JOB_FATAL, 2, -7, INT_MAX};
    for (unsigned i = 0; i < sizeof results / sizeof results[0]; ++i) {
        fixture_job job = make_job(i & 1);
        job.scoped = 1;
        job.null_token = i & 1;
        job.native_result = results[i];
        install_callbacks(7);
        assert(dispatch(&job) == (results[i] == SPU_NATIVE_JOB_COMPLETED ? 1 : -1));
        assert(job.fingerprints == 1 && job.begins == 1 && job.hooks == 1);
        assert(!job.runners && job.ends == 1 && !strcmp(job.order, "BNE"));
    }
    return sizeof results / sizeof results[0];
}

static unsigned fallback_cases(void)
{
    const unsigned masks[] = {0, 3, 7};
    const int results[] = {0, 8, -9};
    for (unsigned i = 0; i < sizeof masks / sizeof masks[0]; ++i) {
        for (unsigned j = 0; j < sizeof results / sizeof results[0]; ++j) {
            fixture_job job = make_job(j & 1);
            job.scoped = masks[i] != 0;
            job.null_token = j == 1;
            job.native_result = SPU_NATIVE_JOB_DECLINED;
            job.runner_result = results[j];
            install_callbacks(masks[i]);
            assert(dispatch(&job) == (results[j] < 0 ? results[j] : 1));
            assert(job.fingerprints == 1 && job.runners == 1);
            assert(job.begins == (unsigned)job.scoped && job.ends == (unsigned)job.scoped);
            assert(job.hooks == (masks[i] == 7));
            assert(!strcmp(job.order, masks[i] == 7 ? "BNRE" : masks[i] == 3 ? "BRE" : "R"));
        }
    }
    return 9;
}

static unsigned invalid_and_miss_cases(void)
{
    unsigned cases = 0;
    for (unsigned mask = 1; mask < 7; ++mask) {
        if (mask == 3) continue;
        fixture_job job = make_job(0);
        install_callbacks(mask);
        assert(dispatch(&job) == -1);
        assert(job.fingerprints == 1 && !job.begins && !job.hooks && !job.runners && !job.ends);
        assert(!job.order_size && !slot_owner);
        ++cases;
    }
    for (unsigned i = 0; i < 5; ++i) {
        fixture_job job = make_job(0);
        job.scoped = 1;
        install_callbacks(7);
        if (i < 2) job.image = NULL;
        if (i == 0 || i == 2) job.image_size = 0;
        if (i == 3) { job.image = image_miss; job.image_size = sizeof image_miss; }
        if (i == 4) { job.image = image_no_entry; job.image_size = sizeof image_no_entry; }
        assert(dispatch(&job) == 0);
        assert(job.fingerprints == (i >= 3));
        assert(!job.begins && !job.hooks && !job.runners && !job.ends && !job.order_size);
        ++cases;
    }
    return cases;
}

static DWORD WINAPI dispatch_thread(LPVOID opaque)
{
    fixture_job* job = (fixture_job*)opaque;
    job->dispatch_result = dispatch(job);
    return 0;
}

static void create_events(fixture_job* job)
{
    job->attempted = CreateEvent(NULL, TRUE, FALSE, NULL);
    job->acquired = CreateEvent(NULL, TRUE, FALSE, NULL);
    job->executing = CreateEvent(NULL, TRUE, FALSE, NULL);
    job->release = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(job->attempted && job->acquired && job->executing && job->release);
}

static void close_events(fixture_job* job)
{
    CloseHandle(job->attempted);
    CloseHandle(job->acquired);
    CloseHandle(job->executing);
    CloseHandle(job->release);
}

static unsigned mixed_scope_cases(void)
{
    for (unsigned native_first = 0; native_first < 2; ++native_first) {
        fixture_job first = make_job(0), second = make_job(1);
        first.scoped = second.scoped = 1;
        first.publish = second.publish = 1;
        first.expected_slot = 0; first.published_slot = 1;
        second.expected_slot = 1; second.published_slot = 2;
        first.native_result = native_first ? SPU_NATIVE_JOB_COMPLETED : SPU_NATIVE_JOB_DECLINED;
        second.native_result = native_first ? SPU_NATIVE_JOB_DECLINED : SPU_NATIVE_JOB_COMPLETED;
        first.block_hook = native_first;
        first.block_runner = !native_first;
        second.null_token = 1;
        create_events(&first); create_events(&second);
        slot_value = 0;
        install_callbacks(7);
        HANDLE workers[2];
        workers[0] = CreateThread(NULL, 0, dispatch_thread, &first, 0, NULL);
        assert(workers[0]);
        assert(WaitForSingleObject(first.executing, 5000) == WAIT_OBJECT_0);
        workers[1] = CreateThread(NULL, 0, dispatch_thread, &second, 0, NULL);
        assert(workers[1]);
        assert(WaitForSingleObject(second.attempted, 5000) == WAIT_OBJECT_0);
        /* The second dispatch has reached begin, but cannot execute while the
         * first native hook or declined path's runner still owns the slot. */
        DWORD early = WaitForSingleObject(second.acquired, 50);
        assert(SetEvent(first.release));
        assert(WaitForMultipleObjects(2, workers, TRUE, 5000) == WAIT_OBJECT_0);
        assert(early == WAIT_TIMEOUT);
        assert(first.dispatch_result == 1 && second.dispatch_result == 1);
        assert(first.begins == 1 && first.ends == 1 && first.hooks == 1);
        assert(second.begins == 1 && second.ends == 1 && second.hooks == 1);
        assert(first.runners == !native_first && second.runners == native_first);
        assert(!strcmp(first.order, native_first ? "BNE" : "BNRE"));
        assert(!strcmp(second.order, native_first ? "BNRE" : "BNE"));
        assert(first.end_sequence < second.execution_sequence);
        assert(slot_value == 2 && !slot_owner);
        CloseHandle(workers[0]); CloseHandle(workers[1]);
        close_events(&first); close_events(&second);
    }
    return 2;
}

int main(void)
{
    InitializeCriticalSection(&slot_lock);
    unsigned cases = native_cases() + fallback_cases() + invalid_and_miss_cases();
    cases += mixed_scope_cases();
    DeleteCriticalSection(&slot_lock);
    printf("Production native dispatch: %u completion, fallback, rejection and scope cases passed.\n", cases);
    return 0;
}
