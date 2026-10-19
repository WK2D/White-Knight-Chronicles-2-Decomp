#ifndef WKC_NATIVE_SKINNING_SIMD_H
#define WKC_NATIVE_SKINNING_SIMD_H
#include "native_skinning.h"
#if defined(__FMA__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
namespace wkc::geometry {
// Requires an FMA-capable x86 target. The live bridge retains RuntimeMath so
// SPU exact/audit modes and exceptional values follow the existing policy.
struct HostFmaMath {
    static Float4 multiply(const Float4& a,const Float4& b) {
        Float4 out;
        _mm_storeu_ps(out.data(),_mm_mul_ps(_mm_loadu_ps(a.data()),_mm_loadu_ps(b.data())));
        return out;
    }
    static Float4 multiply_add(const Float4& a,const Float4& b,const Float4& c) {
        Float4 out;
        _mm_storeu_ps(out.data(),_mm_fmadd_ps(_mm_loadu_ps(a.data()),_mm_loadu_ps(b.data()),_mm_loadu_ps(c.data())));
        return out;
    }
    static Float4 multiply_subtract(const Float4& a,const Float4& b,const Float4& c) {
        Float4 out;
        _mm_storeu_ps(out.data(),_mm_fmsub_ps(_mm_loadu_ps(a.data()),_mm_loadu_ps(b.data()),_mm_loadu_ps(c.data())));
        return out;
    }
};
}
#define WKC_NATIVE_SKINNING_HOST_FMA 1
#endif
#endif
