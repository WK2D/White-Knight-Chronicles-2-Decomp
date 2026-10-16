// Synthetic fragment programs only; no game code or assets are required.
#include "rsx_fp_decompiler.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;

static void checked(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s failed: %08lX\n", operation, result);
        std::exit(1);
    }
}
static ComPtr<ID3DBlob> compile(const char* text, const char* profile) {
    ComPtr<ID3DBlob> code, error;
    HRESULT result = D3DCompile(text, std::strlen(text), "synthetic-precision",
        nullptr, nullptr, "main", profile, D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &error);
    if (FAILED(result) && error)
        std::fprintf(stderr, "%s\n", (const char*)error->GetBufferPointer());
    checked(result, "shader compilation");
    return code;
}
static void word(std::vector<u8>& data, u32 value) {
    value = (value << 16) | (value >> 16);
    for (int shift = 24; shift >= 0; shift -= 8) data.push_back(u8(value >> shift));
}
static void instruction(std::vector<u8>& data, u32 opcode, u32 dest,
                        u32 a, u32 b, u32 c, bool end) {
    word(data, (opcode << 24) | (dest << 1) | (15u << 9) | u32(end));
    word(data, a | (7u << 18)); // Unconditional execution.
    word(data, b);
    word(data, c);
}
static void load(std::vector<u8>& data, u32 reg, const float values[4]) {
    instruction(data, 1, reg, 2u | (0xE4u << 9), 0, 0, false);
    for (int lane = 0; lane < 4; ++lane) {
        u32 bits; std::memcpy(&bits, values + lane, sizeof(bits)); word(data, bits);
    }
}

struct Renderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> view;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11RasterizerState> raster;
    Renderer() {
        // WARP keeps this regression reproducible on machines without a GPU.
        checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context), "device");
        const char* source =
            "float4 main(uint id:SV_VertexID):SV_POSITION {"
            "float2 p=float2((id<<1)&2,id&2);"
            "return float4(p*float2(2,-2)+float2(-1,1),0,1);}";
        auto code = compile(source, "vs_5_0");
        checked(device->CreateVertexShader(code->GetBufferPointer(),
            code->GetBufferSize(), nullptr, &vertex), "vertex shader");
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        checked(device->CreateTexture2D(&desc, nullptr, &target), "target");
        checked(device->CreateRenderTargetView(target.Get(), nullptr, &view), "target view");
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        checked(device->CreateTexture2D(&desc, nullptr, &staging), "readback texture");
        D3D11_RASTERIZER_DESC state = {};
        state.FillMode = D3D11_FILL_SOLID; state.CullMode = D3D11_CULL_NONE;
        state.DepthClipEnable = TRUE;
        checked(device->CreateRasterizerState(&state, &raster), "rasterizer");
    }
    void run(const std::vector<u8>& program, float result[4]) {
        std::vector<char> hlsl(RSX_FP_HLSL_BUFFER_SIZE);
        if (rsx_fp_decompile(program.data(), u32(program.size()), 0x40,
                hlsl.data(), u32(hlsl.size())) <= 0) std::exit(2);
        auto code = compile(hlsl.data(), "ps_5_0");
        ComPtr<ID3D11PixelShader> pixel;
        checked(device->CreatePixelShader(code->GetBufferPointer(),
            code->GetBufferSize(), nullptr, &pixel), "pixel shader");
        context->VSSetShader(vertex.Get(), nullptr, 0);
        context->PSSetShader(pixel.Get(), nullptr, 0);
        context->RSSetState(raster.Get());
        D3D11_VIEWPORT viewport = {0, 0, 1, 1, 0, 1};
        context->RSSetViewports(1, &viewport);
        ID3D11RenderTargetView* rt = view.Get();
        context->OMSetRenderTargets(1, &rt, nullptr);
        const float clear[4] = {-999, -999, -999, -999};
        context->ClearRenderTargetView(rt, clear);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(3, 0);
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        checked(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "readback");
        std::memcpy(result, mapped.pData, sizeof(float) * 4);
        context->Unmap(staging.Get(), 0);
        context->ClearState();
    }
};

int main() {
    Renderer renderer;
    const float values[4] = {-70000, -3, 0.5f, 4};
    unsigned cases = 0;
    for (unsigned source = 0; source < 3; ++source)
    for (unsigned precision = 0; precision <= 5; ++precision)
    for (unsigned modifiers = 0; modifiers < 4; ++modifiers) {
        std::vector<u8> program;
        load(program, 1, values);
        // r2 starts at zero. Use MOV, ADD(0,x), MAD(0,0,x) to isolate each source.
        u32 operands[3] = {8u | (0xE4u << 9), 8u | (0xE4u << 9), 8u | (0xE4u << 9)};
        operands[source] = 4u | (0x1Bu << 9); // r1.wzyx
        if (modifiers & 1) operands[source] |= 1u << (source == 0 ? 29 : 18);
        if (modifiers & 2) operands[source] |= 1u << 17;
        operands[1] |= precision << (19 + 3 * source);
        instruction(program, source == 0 ? 1 : source == 1 ? 3 : 4,
            0, operands[0], operands[1], operands[2], true);
        float result[4]; renderer.run(program, result);
        for (unsigned lane = 0; lane < 4; ++lane) {
            float expected = values[3 - lane];
            if (precision == 4) expected = expected < 0 ? 0 : expected > 1 ? 1 : expected;
            if (modifiers & 1) expected = std::fabs(expected);
            if (precision >= 1 && precision <= 3) {
                float limit = precision == 1 ? 65504.0f : precision == 2 ? 2.0f : 1.0f;
                expected = expected < -limit ? -limit : expected > limit ? limit : expected;
            }
            if (modifiers & 2) expected = -expected;
            if (!std::isfinite(result[lane]) || std::fabs(result[lane] - expected) > 0.0001f) {
                std::fprintf(stderr, "source=%u precision=%u modifiers=%u lane=%u: %g != %g\n",
                    source, precision, modifiers, lane, result[lane], expected);
                return 1;
            }
        }
        ++cases;
    }
    // Negative lighting term: SAT -> LG2 -> EX2 -> ADD(1) must remain finite.
    // The final ADD distinguishes zero from the decompiler's NaN-to-zero guard.
    std::vector<u8> program;
    const float negative[4] = {-0.125f, -1, -2, -4}, one[4] = {1, 1, 1, 1};
    load(program, 1, negative); load(program, 2, one);
    instruction(program, 0x1D, 1, 4u | (0xE4u << 9), 4u << 19, 0, false);
    instruction(program, 0x1C, 0, 4u | (0xE4u << 9), 0, 0, false);
    instruction(program, 3, 0, 0xE4u << 9, 8u | (0xE4u << 9), 0, true);
    float result[4]; renderer.run(program, result);
    for (float lane : result) if (!std::isfinite(lane) || lane != 1.0f) return 1;
    std::printf("%u source-range/modifier cases and negative LG2 lighting chain passed\n", cases);
    return 0;
}
