// Project-written stream algorithm recovered from the geometry skinning routine.
// No guest registers, addresses, local store or generated game code live here.
#ifndef WKC_NATIVE_SKINNING_STREAM_H
#define WKC_NATIVE_SKINNING_STREAM_H
#include "native_skinning.h"
#include <bit>
#include <cstdint>

namespace wkc::geometry {
struct PackedSkinInfluences {
    std::array<std::uint8_t,4> weights;
    std::array<std::uint8_t,4> joints;
};
struct SkinningStreamSpec {
    std::uint32_t vertices;
    std::uint32_t direction_streams;
};
struct SkinningStreamSummary {
    std::array<NormalLengthStages,2> final_direction_lengths;
};
// All nonzero byte weights convert exactly. The caller supplies the conversion
// policy's zero: a host unsigned-to-double lowering can produce -0 under
// downward rounding, whereas exact SPU conversion produces +0.
struct ByteWeightConversion {
    float zero=0;
    Float4 operator()(const std::array<std::uint8_t,4>& bytes) const {
        Float4 out;for(unsigned i=0;i<4;++i)out[i]=bytes[i]?float(bytes[i]):zero;
        return out;
    }
};
struct SkinningStreamPolicy {
    ByteWeightConversion convert_weights;
    Float4 initial_reciprocal_lengths{};
};

// Position-only counterpart using read_influences, read_palette, read_position
// and write_position from the typed IO contract below. All four
// palette entries are read even for zero weights; live palette aliases must see
// prior groups' writes. The outer job handles the palette-enable/skip flags.
// Groups have four vertices, with output commit order 3,0,1,2. This is the
// semantic position stream, not the original routine's register/stack ABI.
template<class Math,class IO>
bool skin_positions_stream(IO& io,std::uint32_t vertices,ByteWeightConversion convert={}) {
    if(!vertices||vertices%4)return false;
    const auto weight_scale=std::bit_cast<float>(std::uint32_t{0x3b808080u});
    const Float4 scale{weight_scale,weight_scale,weight_scale,weight_scale};
    for(std::uint32_t base=0;base<vertices;base+=4) {
        std::array<Float4,4> positions;
        for(unsigned lane=0;lane<4;++lane) {
            const auto packed=io.read_influences(base+lane);
            const auto weights=Math::multiply(convert(packed.weights),scale);
            std::array<CyclicAffine,4> influences;
            for(unsigned joint=0;joint<4;++joint)influences[joint]=io.read_palette(packed.joints[joint]);
            const auto matrix=blend_skin_matrix<Math>(weights,influences);
            positions[lane]=transform_skin_position<Math>(matrix,io.read_position(base+lane)).position;
        }
        for(unsigned lane:{3u,0u,1u,2u})io.write_position(base+lane,positions[lane]);
    }
    return true;
}

// IO owns the decoded stream views:
//   read_influences(vertex) -> PackedSkinInfluences
//   read_position(vertex), read_direction(stream,vertex) -> Float4
//   read_palette(joint) -> CyclicAffine
//   write_position(vertex,value), write_direction(stream,vertex,value)
// Input streams must remain stable for this operation. Palette reads must see
// prior output writes if the provider aliases them. All four joint indices are
// read, including zero-weight influences and indices beyond a declared palette
// count; the provider must define their storage. Do not silently skip them.
// The validated modes have one or two direction streams, disjoint output views
// and a nonzero vertex count divisible by four. Other layouts are not recovered.
// Reciprocal receives four squared lengths and returns four reciprocal lengths;
// the ABI adapter is responsible for the required estimate/interpolation policy.
template<class Math,class NormalMath=Math,class CofactorMath=Math,class IO,class Reciprocal>
bool skin_geometry_stream(IO& io,SkinningStreamSpec spec,Reciprocal reciprocal,SkinningStreamSummary* summary=nullptr,SkinningStreamPolicy policy={}) {
    if(!spec.vertices || spec.vertices%4 || !spec.direction_streams || spec.direction_streams>2)return false;
    using DirectionBatches=std::array<NormalBatch,2>;
    std::array<DirectionBatches,2> pending{};
    const unsigned latency=spec.direction_streams==1?2:1;
    const auto weight_scale=std::bit_cast<float>(std::uint32_t{0x3b808080u});
    const Float4 scale{weight_scale,weight_scale,weight_scale,weight_scale};
    const unsigned groups=spec.vertices/4;
    for(unsigned group=0;group<groups;++group) {
        const unsigned base=group*4;
        std::array<Float4,4> positions;
        DirectionBatches transformed{};
        // Read all palettes for a four-vertex group before committing positions.
        for(unsigned lane=0;lane<4;++lane) {
            const auto packed=io.read_influences(base+lane);
            Float4 weights=policy.convert_weights(packed.weights);std::array<CyclicAffine,4> influences;
            for(unsigned joint=0;joint<4;++joint) {
                influences[joint]=io.read_palette(packed.joints[joint]);
            }
            weights=Math::multiply(weights,scale);
            const auto matrix=blend_skin_matrix<Math>(weights,influences);
            positions[lane]=transform_skin_position<Math>(matrix,io.read_position(base+lane)).position;
            const auto cofactors=cofactor_skin_matrix<CofactorMath>(matrix);
            for(unsigned stream=0;stream<spec.direction_streams;++stream) {
                const auto n=io.read_direction(stream,base+lane);
                const auto first=Math::multiply(n,cofactors[0]);
                const auto second=Math::multiply_add(Float4{n[1],n[2],n[0],n[3]},cofactors[1],first);
                transformed[stream][lane]=Math::multiply_add(Float4{n[2],n[0],n[1],n[3]},cofactors[2],second);
            }
        }
        for(unsigned lane=0;lane<4;++lane)io.write_position(base+lane,positions[lane]);
        DirectionBatches normalized{};
        for(unsigned stream=0;stream<spec.direction_streams;++stream) {
            const auto lengths=normal_length_stages<Math>(transformed[stream]);
            if(summary)summary->final_direction_lengths[stream]=lengths;
            normalized[stream]=scale_skin_normals<NormalMath>(transformed[stream],reciprocal(lengths.length_squared)).normals;
        }
        // One direction stream has a two-group output delay; two streams have
        // a one-group delay. The first write scales an empty batch using the
        // supplied initial reciprocal state (normally zero). In the two-group
        // pipeline the second write normalizes that empty initial batch through
        // the same reciprocal policy. It can produce NaNs rather than zero;
        // aliased palette reads must observe those exact intermediate writes.
        NormalBatch initial{};
        if(group==0)
            initial=scale_skin_normals<NormalMath>(NormalBatch{},policy.initial_reciprocal_lengths).normals;
        if(group==1 && latency==2)
            initial=scale_skin_normals<NormalMath>(NormalBatch{},reciprocal(Float4{})).normals;
        const unsigned slot=group%latency;
        for(unsigned stream=0;stream<spec.direction_streams;++stream)for(unsigned lane=0;lane<4;++lane) {
            const auto vertex=group>=latency?(group-latency)*4+lane:lane;
            io.write_direction(stream,vertex,group>=latency?pending[slot][stream][lane]:initial[lane]);
        }
        pending[slot]=normalized;
    }
    const unsigned first_pending=groups>latency?groups-latency:0;
    for(unsigned group=first_pending;group<groups;++group)
        for(unsigned stream=0;stream<spec.direction_streams;++stream)for(unsigned lane=0;lane<4;++lane)
            io.write_direction(stream,group*4+lane,pending[group%latency][stream][lane]);
    return true;
}
}
#endif
