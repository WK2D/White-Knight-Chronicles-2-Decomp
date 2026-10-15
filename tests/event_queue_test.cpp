#include "sys_event.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <thread>

static uint8_t memory[4096];
extern "C" { uint8_t* vm_base = memory; }
extern "C" uint32_t sys_event_queue_create_direct(uint64_t, int32_t);
extern "C" int sys_event_queue_push_by_id(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t);

// Unused SPU/diagnostic endpoints in the event translation unit. Abort if this
// test accidentally reaches them; queue creation/send/destruction use no SPUs.
extern "C" {
void ydkj_release_pending_threads() { std::abort(); }
void ppu_dump_guest_stack(ppu_context*, const char*) { std::abort(); }
size_t spu_elf_image_size(const uint8_t*, size_t) { std::abort(); }
int spu_workload_dispatch_async(const uint8_t*, uint32_t, uint32_t) { std::abort(); }
int spu_dispatch_frame_by_queue(uint32_t, uint32_t) { std::abort(); }
uint32_t g_spu_pending_evt[3];
int g_spu_pending_evt_valid;
#ifdef _WIN32
int64_t lv2_usec_deadline(uint64_t) { std::abort(); }
int lv2_deadline_passed(int64_t) { std::abort(); }
#endif
}

int main()
{
    // The SPURS completion thread may send while the main thread creates or
    // recycles a queue ID. A send must never enter an invalid host mutex.
    assert(sys_event_queue_create_direct(0, 2) == 1);
    std::atomic<bool> stop{false};
    std::atomic<unsigned> attempts{0};
    std::thread producer([&] {
        while (!stop.load()) {
            sys_event_queue_push_by_id(1, 0x1234, 1, 2, 3);
            ++attempts;
        }
    });
    while (attempts.load() < 100) std::this_thread::yield();
    ppu_context ctx{};
    for (unsigned i = 0; i < 2000; ++i) {
        ctx.gpr[3] = 1;
        assert(sys_event_queue_destroy(&ctx) == CELL_OK);
        // Exercise the guest syscall path as well as direct HLE creation.
        ctx.gpr[3] = 0x100;
        ctx.gpr[4] = 0;
        ctx.gpr[5] = 0;
        ctx.gpr[6] = 2;
        assert(sys_event_queue_create(&ctx) == CELL_OK);
        assert(memory[0x100] == 0 && memory[0x103] == 1);
    }
    stop = true;
    producer.join();
    assert(attempts.load() >= 100);
    ctx.gpr[3] = 1;
    assert(sys_event_queue_destroy(&ctx) == CELL_OK);
    assert(sys_event_queue_push_by_id(1, 0, 0, 0, 0) == -1);
    assert(sys_event_queue_create_direct(0, 2) == 1);
    assert(sys_event_queue_push_by_id(1, 10, 11, 12, 13) == 0);
    assert(sys_event_queue_push_by_id(1, 20, 21, 22, 23) == 0);
    assert(sys_event_queue_push_by_id(1, 30, 31, 32, 33) == -1);
    assert(g_sys_event_queues[0].count == 2);
    assert(g_sys_event_queues[0].buffer[0].source == 10);
    assert(g_sys_event_queues[0].buffer[1].data3 == 23);
    assert(sys_event_queue_destroy(&ctx) == CELL_OK);
}
