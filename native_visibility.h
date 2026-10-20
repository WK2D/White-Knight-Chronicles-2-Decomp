// Authored mask selection and ordered publication over owned records.
// The caller supplies a narrow constructor proof for header initialization.
#ifndef WKC_NATIVE_VISIBILITY_H
#define WKC_NATIVE_VISIBILITY_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
namespace wkc::visibility {
using Byte=std::uint8_t;
enum class HeaderOrigin : Byte {
    unavailable,
    // An owned constructor cleared the header workspace and no current-job
    // operation wrote it. This is a narrow proof, not a hardware SPURS promise.
    constructor_zeroed_untouched_workspace
};
enum class Status : Byte {
    complete, missing_header_origin, short_records, invalid_output, threshold_rejected
};
struct Request {
    std::uint16_t input_mask{};
    std::uint16_t query_count{};
    const Byte* records=nullptr;
    std::size_t record_bytes{};
    std::uint32_t output_ea{};
    std::uint32_t raw_threshold_units{}; // Descriptor BE32+68, not an end EA.
    HeaderOrigin header_origin=HeaderOrigin::unavailable;
};
struct Write {
    std::uint32_t ea{};
    std::array<Byte,16> bytes{};
};
struct Plan {
    std::uint16_t accepted{};
    std::uint32_t final_cursor{};
    // Full sixteen-byte record writes in source order, then the header write.
    std::vector<Write> writes;
};
inline std::uint32_t be32(const Byte* bytes) {
    return(std::uint32_t(bytes[0])<<24)|(std::uint32_t(bytes[1])<<16)|
          (std::uint32_t(bytes[2])<<8)|std::uint32_t(bytes[3]);
}
inline bool keep(std::uint16_t mask,const Byte* record) {
    // The original CEQI(intersection,0), then SFI0, returns1 on ZERO
    // intersection. The wrapper copies records for that true result.
    return (be32(record+4)&std::uint32_t(mask))==0;
}
inline Status plan(const Request& request,Plan& output) {
    if(request.header_origin!=HeaderOrigin::constructor_zeroed_untouched_workspace)
        return Status::missing_header_origin;
    const auto needed=std::size_t(request.query_count)*16u;
    if((needed&&!request.records)||request.record_bytes<needed)return Status::short_records;
    if(request.output_ea<0x10000u||(request.output_ea&15u))return Status::invalid_output;
    unsigned accepted=0;
    for(unsigned i=0;i<request.query_count;++i)accepted+=keep(request.input_mask,request.records+std::size_t(i)*16u);
    const auto cursor=std::uint64_t(request.output_ea)+(std::uint64_t(accepted)+1u)*16u;
    if(cursor>0xffffffffull)return Status::invalid_output;
    // Preserve the literal unsigned comparison, including its32-bit shift.
    // The zero-query path bypasses it in the original.
    if(request.query_count&&std::uint32_t(cursor)<=std::uint32_t(request.raw_threshold_units<<4))
        return Status::threshold_rejected;
    Plan result;result.accepted=static_cast<std::uint16_t>(accepted);result.final_cursor=static_cast<std::uint32_t>(cursor);
    result.writes.reserve(std::size_t(accepted)+1u);
    auto destination=request.output_ea+16u;
    for(unsigned i=0;i<request.query_count;++i) {
        const auto* record=request.records+std::size_t(i)*16u;
        if(!keep(request.input_mask,record))continue;
        Write write;write.ea=destination;std::memcpy(write.bytes.data(),record,16);
        result.writes.push_back(write);destination+=16u;
    }
    Write header;header.ea=request.output_ea;
    header.bytes[0]=static_cast<Byte>(accepted>>8);header.bytes[1]=static_cast<Byte>(accepted);
    result.writes.push_back(header);output=std::move(result);return Status::complete;
}
}
#endif
