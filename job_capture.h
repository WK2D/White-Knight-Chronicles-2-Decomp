// Optional capture of complete SPURS job transactions for differential replay.
//
// WKC_JOB_CAPTURE=<directory> with WKC_JOB_CAPTURE_IMAGE=<image id> records,
// for each dispatched job of that image (up to WKC_JOB_CAPTURE_COUNT, default
// 64), the job descriptor and every DMA GET and PUT the job performs, in order,
// with their bytes. One file per job: job-NNNNN.bin. Off unless both are set.
// Recording starts when <directory>/arm exists, so a route can pick the scene.
//
// File layout (little-endian): "WKCJ" u32 version=1, u32 image, u32 job_ea,
// u32 descriptor_size, descriptor bytes, then records until end of file:
// u8 kind ('G' read, 'P' write), u32 ea, u32 size, u32 spu_pc, bytes.
// Files hold guest data: keep them private.
#pragma once
#include <cstdint>

namespace wkc::job_capture {
void configure();
bool enabled();
// Called from the job scope callbacks on the dispatching thread.
void begin(int image,std::uint32_t job_ea,std::uint32_t descriptor_size);
void end(int image);
// DMA trace callbacks (any thread; only the capturing thread's job is recorded).
void get(std::uint32_t ea,std::uint32_t size,int image,std::uint32_t pc,const std::uint8_t* bytes);
void put(std::uint32_t ea,std::uint32_t size,int image,std::uint32_t pc,const std::uint8_t* bytes);
}
