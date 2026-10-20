#ifndef WKC_NATIVE_SKINNING_BRIDGE_H
#define WKC_NATIVE_SKINNING_BRIDGE_H
struct spu_context;
#ifdef __cplusplus
extern "C" {
#endif
// One image-19 block prefix. Zero means use its unchanged compiled reference.
// No LS/DMA writes, PC/status changes or continuation dispatch occur here.
int wkc_native_blend_geometry(struct spu_context* ctx);
// After the blend prefix, transform four decoded positions. Other layouts
// decline unchanged and retain the compiled tail. Only five GPRs are written.
int wkc_native_transform_geometry(struct spu_context* ctx);
// Decode four cofactor matrices into 24 packed native float4 vectors: first
// 12 cofactor vectors, then their 12 rounded products, each vertex then cyclic
// phase. Requires at least 384 output bytes and no alias
// with ctx. No architectural state is written, including on success.
int wkc_native_cofactor_geometry(const struct spu_context* ctx, void* output, unsigned bytes);
// Complete the supported post-blend batch boundary, including ordered LS
// output/read-ahead/stack writes and all live/scratch GPR results. Rejects
// unsupported layouts without writes. Host/audit nonfinite inputs retain the
// compiled path for NaN-payload compatibility; exact mode has no value guard.
// The caller owns PC/continuation dispatch.
int wkc_native_finish_geometry(struct spu_context* ctx);
// Final two batches of four normals, including squared lengths, SPU estimate
// interpolation, ordered output stores and scratch/live register export.
// Unsupported control/packing declines before writes. Arithmetic uses the SDK
// float policy. The caller retains PC/continuation dispatch. No DMA/channel calls.
int wkc_native_finish_normals(struct spu_context* ctx);
#ifdef __cplusplus
}
#endif
#endif
