/* Synthetic NV40 programs only. Exercise the production decompiler and
 * SPIR-V compiler without a game executable or a graphics device. */
#include "rsx_fp_decompiler.h"
#include "rsx_shader_spirv.h"
#include "rsx_test_programs.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

static bool require(bool value, const char* message)
{
    if (!value) std::fprintf(stderr, "%s\n", message);
    return value;
}

static unsigned program(u8* code, u32 opcode, bool mixed)
{
    const u32 source = RSX_TEST_FP_SRC_INPUT | RSX_TEST_FP_SWZ_IDENT;
    rsx_test_fp_instr(code, RSX_TEST_FP_OPC(opcode) | RSX_TEST_FP_MASK_XYZW |
        RSX_TEST_FP_INSRC_TC0 | RSX_TEST_FP_TEXU(4) | RSX_TEST_FP_END, source);
    if (!mixed) return 16;
    std::memcpy(code + 16, code, 16);
    rsx_test_fp_instr(code, RSX_TEST_FP_OPC(RSX_TEST_FP_OP_TEX) |
        RSX_TEST_FP_MASK_XYZW | RSX_TEST_FP_OUTREG(1) |
        RSX_TEST_FP_INSRC_TC0 | RSX_TEST_FP_TEXU(3), source);
    rsx_test_fp_put_word(code + 16, RSX_TEST_FP_OPC(opcode) |
        RSX_TEST_FP_MASK_XYZW | RSX_TEST_FP_INSRC_TC0 | RSX_TEST_FP_TEXU(4));
    rsx_test_fp_instr(code + 32, RSX_TEST_FP_OPC(RSX_TEST_FP_OP_MOV) |
        (2u << 9) | RSX_TEST_FP_END, (1u << 2) | RSX_TEST_FP_SWZ_IDENT);
    return 48;
}

static bool compile(const char* hlsl, bool explicit_lod, bool mixed)
{
    std::vector<u32> words(65536);
    u32 count = 0;
    char log[8192] = {};
    if (!require(rsx_hlsl_to_spirv(hlsl, RSX_SHADER_STAGE_FRAGMENT,
        words.data(), (u32)words.size(), &count, log, sizeof log) == 0, log))
        return false;
    std::set<u32> bindings;
    unsigned compare = 0, ordinary = 0;
    for (u32 i = 5; i < count;) {
        const u32 size = words[i] >> 16, op = words[i] & 0xffffu;
        if (!require(size && size <= count - i, "Malformed SPIR-V instruction")) return false;
        /* OpImageSampleDrefImplicitLod/ExplicitLod, ordinary equivalents. */
        compare += op == (explicit_lod ? 90u : 89u);
        ordinary += op == 87u;
        if (op == 71u && size == 4 && words[i + 2] == 33u)
            bindings.insert(words[i + 3]);
        if (op == 71u && size == 4 && words[i + 2] == 34u &&
            !require(words[i + 3] == 0, "Unexpected descriptor set")) return false;
        i += size;
    }
    const std::set<u32> expected = mixed ? std::set<u32>{19,20,51,52} : std::set<u32>{20,52};
    return require(compare == 1, "Expected exactly one depth-reference sample") &&
        require(ordinary == (mixed ? 1u : 0u), "Ordinary unit lost or became a comparison") &&
        require(bindings == expected, "Per-unit image/sampler bindings disagree with Vulkan layout");
}

int main()
{
    static char hlsl[RSX_FP_HLSL_BUFFER_SIZE], legacy[RSX_FP_HLSL_BUFFER_SIZE];
    for (u32 opcode : {0x17u,0x18u,0x2fu,0x31u}) {
        for (bool mixed : {false,true}) {
            u8 code[48] = {};
            const unsigned bytes = program(code, opcode, mixed);
            u32 constants = 99;
            if (!require(rsx_fp_decompile_buffered_shadow_ex(code, bytes, 0x40, 0,
                    1u << 4, hlsl, sizeof hlsl, &constants) > 0 && constants == 0,
                    "Synthetic comparison program failed to decompile")) return 1;
            if (!compile(hlsl, opcode == 0x2f || opcode == 0x31, mixed)) return 2;
            const u32 dimensions[16][2] = {{0,0},{0,0},{0,0},{64,32},{128,64}};
            if (!require(rsx_fp_apply_unnorm_scale(hlsl, sizeof hlsl,
                    (1u << 3) | (1u << 4), dimensions, 0) >= 1,
                    "Unnormalized comparison coordinates were not scaled")) return 3;
            if (!compile(hlsl, opcode == 0x2f || opcode == 0x31, mixed)) return 4;
            for (u32 cube : {0u,1u << 3}) {
                if (!require(rsx_fp_decompile_buffered_ex(code, bytes, 0x40, cube,
                        legacy, sizeof legacy, nullptr) > 0 &&
                        rsx_fp_decompile_buffered_shadow_ex(code, bytes, 0x40, cube,
                        0, hlsl, sizeof hlsl, nullptr) > 0 && !std::strcmp(hlsl, legacy),
                        "Zero comparison mask changed a legacy backend shader")) return 5;
            }
            if (!require(rsx_fp_decompile_buffered_shadow_ex(code, bytes, 0x40,
                    1u << 4, 1u << 4, hlsl, sizeof hlsl, nullptr) < 0,
                    "Overlapping cube and comparison masks were accepted")) return 6;
        }
    }
    std::puts("Comparison SPIR-V, mixed unit bindings, coordinate scaling and legacy API checks pass.");
    return 0;
}
