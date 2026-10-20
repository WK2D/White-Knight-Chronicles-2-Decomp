// Reconstruct filtered index export bytes from logical indices and keep decisions.
// Logical input indices + one explicit keep decision per triangle. No captures,
// reference-produced padding, guest registers, LS addresses or generated code.
#ifndef WKC_NATIVE_INDEX_PAYLOAD_H
#define WKC_NATIVE_INDEX_PAYLOAD_H
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>
namespace wkc::geometry::index_payload {
// Structural rejects preserve payload and logical_count. Computation uses owned
// scratch and commits only after all input reads, including when a caller's
// input views happen to point into payload's old storage.
// Filtering path only. Bypass mode requires independently decoded physical
// padding and is intentionally outside this candidate's interface. The caller
// must provide decisions directly from native filtering, not infer ambiguous
// duplicate decisions by greedily matching the final index array.
inline bool reconstruct(std::span<const std::uint16_t> indices,
                        std::span<const std::uint8_t> keep,
                        std::vector<std::uint8_t>& payload,
                        std::uint32_t& logical_count) {
 if(indices.size()>65535||indices.size()%3||keep.size()!=indices.size()/3)return false;
 for(const auto value:keep)if(value>1)return false;
 if(indices.empty()){std::vector<std::uint8_t> empty;payload=std::move(empty);logical_count=0;return true;}
 const std::size_t arena_end=(indices.size()*2+15)&~std::size_t(15);
 // Every loaded output prefix is produced by an earlier store; initial zero
 // scratch is not a claimed source of exported padding. Zeros in each compact
 // block below are the defined SHUFB-zero positions of the recovered algorithm.
 std::vector<std::uint8_t> memory(arena_end+32);
 std::size_t cursor=0;
 for(std::size_t group=0;group<keep.size();group+=4) {
  std::array<std::uint8_t,32> block{};std::size_t bytes=0;
  for(std::size_t lane=0;lane<4&&group+lane<keep.size();++lane)if(keep[group+lane]) {
   for(unsigned v=0;v<3;++v) {
    const auto word=indices[(group+lane)*3+v];
    block[bytes++]=std::uint8_t(word>>8);block[bytes++]=std::uint8_t(word);
   }
  }
  const std::size_t offset=cursor%16,line=cursor&~std::size_t(15);
  std::copy_n(block.begin(),16-offset,memory.begin()+line+offset);
  // The original spills later lines to private stack scratch when the current
  // cursor is too close to the original input arena end; do not overwrite the
  // external index buffer at those redirected addresses.
  if(arena_end>=17&&cursor<=arena_end-17)
   std::copy_n(block.begin()+16-offset,16,memory.begin()+line+16);
  if(arena_end>=33&&cursor<=arena_end-33)
   for(std::size_t at=0;at<16;++at)
    memory[line+32+at]=block[16+(at+16-offset)%16];
  cursor+=bytes;
 }
 memory.resize((cursor+15)&~std::size_t(15));
 const auto count=std::uint32_t(cursor/2);
 payload=std::move(memory);logical_count=count;return true;
}
}
#endif
