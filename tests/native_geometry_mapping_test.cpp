#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native_geometry_mapping_cache.h"
#include <cassert>
#include <cstdio>
#include <vector>
using wkc::geometry::mapping::ScopedCache;
using wkc::geometry::mapping::Mapping;
struct FakeBackend {
    std::vector<Mapping> mappings;
    unsigned queries=0,commits=0;
    bool commit_ok=true;
    bool query(std::uint32_t ea,Mapping& output)noexcept {
        ++queries;
        for(const auto& region:mappings)if(region.begin<=ea&&ea<region.end){output=region;return true;}
        return false;
    }
    bool commit(std::uint32_t page)noexcept {
        ++commits;if(!commit_ok)return false;
        for(auto& region:mappings)if(region.begin==page&&region.reserved) {
            region.committed=true;region.reserved=false;region.readable=region.writable=true;return true;
        }
        return false;
    }
};
int main() {
    unsigned cases=0;
    auto* const base=reinterpret_cast<void*>(std::uintptr_t(0x100000000ull));
    ScopedCache cache;
    FakeBackend fake{{{0,0x40000,true,true,false,true,true}}};
    cache.begin(base);
    assert(cache.check(base,7,8,true,fake)&&fake.queries==1);++cases;
    for(unsigned i=0;i<10000;++i)assert(cache.check(base,0x1ffff,2,true,fake));
    assert(fake.queries==1);++cases; // Whole validated region, across64KiB pages.
    cache.end();cache.begin(base);
    assert(cache.check(base,7,8,true,fake)&&fake.queries==2);++cases;
    cache.end();cache.begin_lifetime(base,17);
    assert(cache.check(base,7,8,true,fake));
    const auto lifetime_queries=fake.queries;
    cache.suspend();cache.begin_lifetime(base,17);
    assert(cache.check(base,7,8,true,fake)&&fake.queries==lifetime_queries);++cases;
    cache.suspend();cache.begin_lifetime(base,18);
    assert(cache.check(base,7,8,true,fake)&&fake.queries==lifetime_queries+1);++cases;
    cache.suspend();cache.begin_lifetime(base,0);
    assert(cache.check(base,7,8,true,fake));
    cache.suspend();cache.begin_lifetime(base,0);
    assert(cache.check(base,7,8,true,fake)&&fake.queries==lifetime_queries+3);++cases;
    fake.mappings={
        {0,0x1000,true,true,false,true,true},
        {0x1000,0x2000,true,true,false,true,false},
        {0x2000,0x3000,true,true,false,false,false},
        {0x3000,0x4000,true,true,false,true,true}};
    cache.invalidate();
    assert(cache.check(base,0xfff,2,false,fake));++cases;
    assert(!cache.check(base,0xfff,2,true,fake));++cases;
    assert(!cache.check(base,0x1fff,0x1002,false,fake));++cases;
    assert(cache.check(base,0x3000,1,true,fake));++cases;
    cache.invalidate();fake.mappings[0].readable=fake.mappings[0].writable=false;
    assert(!cache.check(base,0,1,false,fake));++cases;
    cache.end();cache.begin(base);fake.mappings={{0,0x10000,true,false,true,false,false}};
    assert(cache.check(base,0xfff,2,true,fake)&&fake.commits==1);++cases;
    cache.invalidate();fake.mappings={{0,0x10000,false,false,true,false,false}};
    assert(!cache.check(base,0,1,true,fake)&&fake.commits==1);++cases;
    fake.mappings={{0,0x10000,true,false,true,false,false}};fake.commit_ok=false;
    assert(!cache.check(base,0,1,true,fake)&&fake.commits==2);++cases;
    fake.mappings={{0,0x10000,true,true,false,true,true}};cache.invalidate();
    assert(!cache.check(base,0,0,true,fake)&&!cache.check(base,0xffffffffu,2,true,fake));++cases;
    assert(cache.check(base,0,1,true,fake));
    fake.mappings[0].readable=fake.mappings[0].writable=false;
    auto* const remapped=reinterpret_cast<void*>(std::uintptr_t(0x200000000ull));
    assert(!cache.check(remapped,0,1,true,fake));++cases; // Base mismatch cannot hit old permissions.

#ifdef _WIN32
    using wkc::geometry::mapping::WindowsBackend;
    auto* memory=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x40000,MEM_RESERVE,PAGE_NOACCESS));
    assert(memory);
    WindowsBackend windows{memory};cache.end();cache.begin(memory);
    assert(cache.check(memory,0x8000,16,true,windows)&&windows.commits==1);++cases;
    const auto queries=windows.queries;
    for(unsigned i=0;i<10000;++i)assert(cache.check(memory,0x8000,16,true,windows));
    assert(windows.queries==queries);++cases;
    DWORD old=0;assert(VirtualProtect(memory+0x1000,0x1000,PAGE_READONLY,&old));
    cache.invalidate();
    assert(cache.check(memory,0xfff,2,false,windows)&&!cache.check(memory,0xfff,2,true,windows));++cases;
    assert(VirtualProtect(memory+0x2000,0x1000,PAGE_READWRITE|PAGE_GUARD,&old));
    cache.invalidate();
    assert(!cache.check(memory,0x1fff,0x1002,false,windows));++cases;
    assert(VirtualProtect(memory+0x2000,0x1000,PAGE_NOACCESS,&old));cache.end();cache.begin(memory);
    assert(!cache.check(memory,0x2000,1,false,windows));++cases;
    assert(VirtualProtect(memory+0x2000,0x1000,PAGE_READWRITE,&old));cache.invalidate();
    assert(cache.check(memory,0x2000,1,true,windows));++cases;
    assert(VirtualFree(memory+0x10000,0x10000,MEM_DECOMMIT));cache.end();cache.begin(memory);
    const auto commits=windows.commits;
    assert(cache.check(memory,0x10000,16,true,windows)&&windows.commits==commits+1);++cases;
    cache.end();assert(VirtualFree(memory,0,MEM_RELEASE));
    // Reuse the SAME VM base with entirely different protection; epoch comes
    // from dispatch lifetime rather than an unreliable base-only identity.
    auto* same=static_cast<std::uint8_t*>(VirtualAlloc(memory,0x40000,MEM_RESERVE,PAGE_NOACCESS));
    assert(same==memory&&VirtualAlloc(same,0x10000,MEM_COMMIT,PAGE_READONLY));
    windows={same};cache.begin(same);
    assert(cache.check(same,0,16,false,windows)&&!cache.check(same,0,16,true,windows));++cases;
    cache.end();assert(VirtualFree(same,0,MEM_RELEASE));
#endif
    cache.end();fake.mappings={{{0,0x10000,true,true,false,true,true}}};
    cache.begin_lifetime(base,100);assert(cache.check(base,0,16,true,fake));
    fake.mappings[0].writable=false;cache.suspend();cache.begin_lifetime(base,101);
    assert(cache.check(base,0,16,false,fake)&&!cache.check(base,0,16,true,fake));++cases;
    std::printf("Scoped native mapping cache: %u permission/range/guard/commit/remap/epoch cases passed; 10000 repeated spans issue no extra queries.\n",cases);
}
