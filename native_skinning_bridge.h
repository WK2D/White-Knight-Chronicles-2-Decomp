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
#ifdef __cplusplus
}
#endif
#endif
