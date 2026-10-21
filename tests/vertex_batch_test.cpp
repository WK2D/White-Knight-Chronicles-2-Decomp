// Synthetic inputs only. Compare the production batch path with fetch_one.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_vertex_compact.h"
#include "rsx_vertex_formats.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static u8 memory[4096];
static unsigned resolver_calls;

static const u8* guest(void*, u32, u32 offset, u32 bytes)
{
    ++resolver_calls;
    return offset + u64(bytes) <= sizeof memory ? memory + offset : nullptr;
}

static u32 random_state = 0x12781573;
static u32 random32()
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static unsigned cases;
static void compare(const rsx_vertex_fetch_plan& plan,
                    const rsx_vertex_ref* refs, unsigned count)
{
    u8 baseline[1024], batch[1024];
    assert(count * plan.layout.stride <= sizeof baseline);
    memset(baseline, 0x5a, sizeof baseline);
    memset(batch, 0x5a, sizeof batch);
    int expected = 1;
    for (unsigned i = 0; i < count; ++i) {
        if (!rsx_vertex_fetch_one(&plan, refs + i,
                                 baseline + i * plan.layout.stride)) {
            expected = 0;
            break;
        }
    }
    const int got = rsx_vertex_fetch_batch(&plan, refs, count, batch);
    assert(expected == got);
    // Failed draws are discarded; their partial output is unspecified.
    if (expected && memcmp(baseline, batch, count * plan.layout.stride)) {
        for (unsigned i = 0; i < count * plan.layout.stride; ++i) {
            if (baseline[i] != batch[i]) {
                fprintf(stderr, "case=%u byte=%u expected=%02x got=%02x type=%u size=%u frequency=%u divider=%x stable=%u..%u inline=%d\n",
                        cases, i, baseline[i], batch[i], plan.attr[plan.layout.attrs[i % plan.layout.stride / 16]].desc.type,
                        plan.attr[0].desc.size, plan.attr[0].desc.frequency,
                        plan.divider_mask, plan.attr[0].stable_first,
                        plan.attr[0].stable_last, plan.inline_data != nullptr);
                break;
            }
        }
        assert(false);
    }
    assert(!memcmp(batch + count * plan.layout.stride,
                   baseline + count * plan.layout.stride,
                   sizeof batch - count * plan.layout.stride));
    ++cases;
}

static void differential()
{
    rsx_vertex_fetch_plan plan{};
    plan.layout.count = 3;
    plan.layout.mask = 0x19;
    plan.layout.stride = 48;
    plan.layout.attrs[0] = 0;
    plan.layout.attrs[1] = 3;
    plan.layout.attrs[2] = 4;
    plan.guest_ptr = guest;
    rsx_vertex_ref refs[16];
    for (unsigned i = 0; i < 16; ++i)
        refs[i] = {i + (i % 3), i % 3};

    for (unsigned type = 0; type < 10; ++type)
    for (unsigned size = 0; size < 6; ++size)
    for (unsigned frequency = 0; frequency < 5; ++frequency)
    for (unsigned modulo = 0; modulo < 2; ++modulo)
    for (unsigned stable = 0; stable < 3; ++stable) {
        for (auto& byte : memory)
            byte = u8(random32());
        plan.divider_mask = modulo ? 0x19 : 0;
        for (unsigned attr : {0u, 3u, 4u}) {
            auto& fetch = plan.attr[attr];
            fetch.desc.type = type;
            fetch.desc.size = size;
            fetch.desc.frequency = frequency;
            fetch.desc.offset = 32 + attr * 128;
            fetch.desc.location = attr % 2;
            fetch.stride = 20;
            fetch.elem_size = rsx_vertex_attrib_size(type, size);
            // Complete, partial and absent prepared mappings.
            fetch.stable_first = stable == 2 ? 3 : 0;
            fetch.stable_last = stable == 2 ? 7 : 31;
            fetch.stable_base = stable
                ? memory + fetch.desc.offset + fetch.stable_first * fetch.stride
                : nullptr;
            for (auto& value : fetch.default_value) {
                const u32 bits = random32();
                memcpy(&value, &bits, sizeof value);
            }
        }
        compare(plan, refs, 16);
        plan.inline_data = memory;
        compare(plan, refs, 16);
        plan.inline_data = nullptr;
    }

    // Every half representation: signed zero, subnormals, infinity, NaN payloads.
    plan.layout.count = 1;
    plan.layout.stride = 16;
    plan.divider_mask = 0;
    auto& fetch = plan.attr[0];
    fetch.desc.type = RSX_VERTEX_TYPE_SF;
    fetch.desc.size = 1;
    fetch.desc.frequency = 0;
    fetch.elem_size = 2;
    fetch.stable_base = memory;
    fetch.stable_first = fetch.stable_last = 0;
    fetch.stride = 2;
    const rsx_vertex_ref zero{};
    for (unsigned half = 0; half < 65536; ++half) {
        memory[0] = u8(half >> 8);
        memory[1] = u8(half);
        compare(plan, &zero, 1);
    }

    // The street's formats through the fixed-size paths: every half in every
    // lane of half2-4, every byte in every lane of unorm8x4, and packed
    // 11-11-10 words with all sign and magnitude patterns.
    {
        rsx_vertex_ref refs[16];
        for (unsigned i = 0; i < 16; ++i)
            refs[i] = {i, 0};
        fetch.stable_first = 0;
        fetch.stable_last = 15;
        fetch.desc.frequency = 0;
        for (unsigned size = 2; size <= 4; ++size) {
            fetch.desc.type = RSX_VERTEX_TYPE_SF;
            fetch.desc.size = size;
            fetch.elem_size = fetch.stride = 2 * size;
            for (unsigned half = 0; half < 65536; half += 16)
                for (unsigned lane = 0; lane < size; ++lane) {
                    for (auto& byte : memory)
                        byte = u8(random32());
                    for (unsigned v = 0; v < 16; ++v) {
                        memory[v * fetch.stride + lane * 2] = u8((half + v) >> 8);
                        memory[v * fetch.stride + lane * 2 + 1] = u8(half + v);
                    }
                    compare(plan, refs, 16);
                }
        }
        fetch.desc.type = RSX_VERTEX_TYPE_UB;
        fetch.desc.size = 4;
        fetch.elem_size = fetch.stride = 4;
        for (unsigned value = 0; value < 256; value += 16) {
            for (unsigned v = 0; v < 64; ++v)
                memory[v] = u8(value + v % 16 + v / 16 * 61);
            compare(plan, refs, 16);
        }
        fetch.desc.type = RSX_VERTEX_TYPE_CMP;
        for (unsigned size = 1; size <= 4; ++size) {
            fetch.desc.size = size;
            for (unsigned round = 0; round < 4096; ++round) {
                for (unsigned v = 0; v < 64; ++v)
                    memory[v] = u8(random32());
                compare(plan, refs, 16);
            }
        }
        fetch.desc.type = RSX_VERTEX_TYPE_SF;
        fetch.desc.size = 1;
        fetch.elem_size = fetch.stride = 2;
        fetch.stable_last = 0;
    }

    // Invalid enabled position is fatal; other attributes use their exact defaults.
    fetch.stable_base = nullptr;
    fetch.desc.offset = sizeof memory;
    compare(plan, &zero, 1);
    plan.layout.attrs[0] = 3;
    plan.attr[3] = fetch;
    for (auto& value : plan.attr[3].default_value) {
        const u32 bits = random32();
        memcpy(&value, &bits, sizeof value);
    }
    compare(plan, &zero, 1);
    plan.guest_ptr = nullptr;
    compare(plan, &zero, 1);
    plan.attr[3].desc.type = 9;
    compare(plan, &zero, 1);

    // Empty draws and malformed API arguments cannot dereference output/refs.
    assert(rsx_vertex_fetch_batch(&plan, nullptr, 0, nullptr));
    assert(!rsx_vertex_fetch_batch(nullptr, &zero, 1, memory));
    assert(!rsx_vertex_fetch_batch(&plan, nullptr, 1, memory));
    assert(!rsx_vertex_fetch_batch(&plan, &zero, 1, nullptr));
    plan.layout.count = 17;
    assert(!rsx_vertex_fetch_batch(&plan, &zero, 1, memory));
    plan.layout.count = 1;
    plan.layout.attrs[0] = 16;
    assert(!rsx_vertex_fetch_batch(&plan, &zero, 1, memory));
    plan.layout.attrs[0] = 0;
    plan.layout.stride = 1;
    assert(!rsx_vertex_fetch_batch(&plan, &zero, 1, memory));
    plan.layout.stride = 0;
    assert(rsx_vertex_fetch_batch(&plan, &zero, 1, nullptr));
    assert(resolver_calls > 0);
    printf("%u differential cases: byte-identical successful outputs, matching failures\n", cases);
}

static void benchmark()
{
    rsx_vertex_fetch_plan plan{};
    rsx_vertex_ref refs[16];
    plan.layout.count = 3;
    plan.layout.stride = 48;
    for (unsigned i = 0; i < 16; ++i)
        refs[i] = {i, 0};
    for (unsigned slot = 0; slot < 3; ++slot) {
        plan.layout.attrs[slot] = u8(slot);
        auto& fetch = plan.attr[slot];
        fetch.desc.type = RSX_VERTEX_TYPE_F;
        fetch.desc.size = 3;
        fetch.stable_base = memory;
        fetch.stable_first = 0;
        fetch.stable_last = 15;
        fetch.stride = 12;
    }
    u8 out[768];
    for (int mode = 0; mode < 2; ++mode) {
        u32 checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 500000; ++i) {
            if (mode)
                assert(rsx_vertex_fetch_batch(&plan, refs, 16, out));
            else
                for (unsigned v = 0; v < 16; ++v)
                    assert(rsx_vertex_fetch_one(&plan, refs + v, out + v * 48));
            checksum += out[i % sizeof out];
        }
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        printf("mode=%d fixed work ns=%lld checksum=%u\n", mode,
               static_cast<long long>(ns), checksum);
    }
}

int main(int argc, char** argv)
{
    differential();
    if (argc == 2 && !strcmp(argv[1], "--benchmark"))
        benchmark();
}
