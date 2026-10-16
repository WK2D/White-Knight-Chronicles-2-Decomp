/* Optional, read-only provenance for writes to a guest command-buffer region. */
#include "ppu_recomp.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>

extern "C" {
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern void (*g_ppu_store_trace_hook)(uint32_t, uint32_t, int, void*);
extern void (*g_spu_put_trace_hook)(uint32_t, uint32_t, int, uint32_t, const uint8_t*);
uint32_t ppu_prof_resolve_host(void*);
}

namespace {
struct Writer {
    uint64_t sequence = 0;
    void* host_return = nullptr;
    uint32_t begin = 0, size = 0, id = 0, pc = 0, lr = 0, value = 0;
};
struct History { Writer current, previous; };
std::unique_ptr<History[]> history;
std::mutex history_mutex;
uint32_t region_begin = 0, region_size = 0;
uint64_t sequence = 0;
constexpr uint32_t spu_marker = 0x80000000u;

void record(Writer writer, const uint8_t* source = nullptr) {
    uint64_t end = uint64_t(writer.begin) + writer.size;
    uint64_t region_end = uint64_t(region_begin) + region_size;
    if (!writer.size || writer.begin >= region_end || end <= region_begin) return;
    uint32_t first = writer.begin < region_begin ? region_begin : writer.begin;
    uint64_t last = end < region_end ? end : region_end;
    std::lock_guard lock(history_mutex);
    writer.sequence = ++sequence;
    for (uint32_t word = first & ~3u; uint64_t(word) < last; word += 4) {
        auto& slot = history[(word - region_begin) / 4];
        slot.previous = slot.current;
        if (source && word >= writer.begin && uint64_t(word) + 4 <= end) {
            const auto* p = source + (word - writer.begin);
            writer.value = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
                           uint32_t(p[2]) << 8 | p[3];
        }
        slot.current = writer;
    }
}

void ppu_store(uint32_t ea, uint32_t value, int width, void* host_return) {
    if (ea >= uint64_t(region_begin) + region_size ||
        uint64_t(ea) + unsigned(width) <= region_begin) return;
    auto* ctx = g_active_ctx;
    Writer writer;
    writer.begin = ea; writer.size = unsigned(width); writer.value = value;
    writer.host_return = host_return;
    writer.id = ctx ? uint32_t(ctx->thread_id) : 0;
    writer.lr = ctx ? uint32_t(ctx->lr) : 0;
    record(writer);
}

void spu_put(uint32_t ea, uint32_t size, int image, uint32_t pc, const uint8_t* source) {
    Writer writer;
    writer.begin = ea; writer.size = size;
    writer.id = spu_marker | uint32_t(image); writer.pc = pc;
    record(writer, source);
}
}

extern "C" void wkc2_init_graphics_writers(void) {
    /* Supply the mapped command-buffer EA and byte length as hexadecimal.
     * Keeping this explicit avoids observing unrelated guest allocations. */
    const char* region = std::getenv("WKC2_FIFO_REGION");
    if (!std::getenv("WKC2_FIFO_TRACE") || !region) return;
    char* next = nullptr;
    uint64_t begin = std::strtoull(region, &next, 16);
    if (*next != ':') return;
    char* end = nullptr;
    uint64_t size = std::strtoull(next + 1, &end, 16);
    if (*end || (begin & 3) || (size & 3) || !size || size > 0x1000000 ||
        begin + size > UINT32_MAX) return;
    history = std::make_unique<History[]>(size / 4);
    region_begin = uint32_t(begin); region_size = uint32_t(size);
    g_ppu_store_trace_hook = ppu_store;
    g_spu_put_trace_hook = spu_put;
    std::fprintf(stderr, "[graphics-writers] observing %08X+%X; PPU stores/atomics and SPU DMA PUTs only\n",
                 region_begin, region_size);
}

extern "C" void wkc2_dump_graphics_writers(uint32_t center) {
    if (!history) return;
    std::lock_guard lock(history_mutex);
    for (int offset = -16; offset <= 32; offset += 4) {
        uint32_t ea = center + uint32_t(offset);
        if (ea < region_begin || ea >= uint64_t(region_begin) + region_size) continue;
        const auto& slot = history[(ea - region_begin) / 4];
        for (const auto* writer : {&slot.previous, &slot.current}) {
            if (!writer->sequence) continue;
            bool spu = (writer->id & spu_marker) != 0;
            uint32_t pc = spu ? writer->pc : ppu_prof_resolve_host(writer->host_return);
            std::fprintf(stderr, "[graphics-writer] ea=%08X sequence=%llu %s=%u pc=%08X lr=%08X range=%08X+%X value=%08X\n",
                ea, (unsigned long long)writer->sequence, spu ? "image" : "tid",
                writer->id & ~spu_marker, pc, writer->lr,
                writer->begin, writer->size, writer->value);
        }
    }
}
