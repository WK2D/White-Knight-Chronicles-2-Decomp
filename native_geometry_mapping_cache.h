#ifndef WKC_NATIVE_GEOMETRY_MAPPING_CACHE_H
#define WKC_NATIVE_GEOMETRY_MAPPING_CACHE_H
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace wkc::geometry::mapping {
struct Mapping {
    std::uint64_t begin=0,end=0;
    bool own_reservation=false,committed=false,reserved=false,readable=false,writable=false;
};
// Submitted mappings remain pinned until each synchronous dispatch returns.
// Scoped users discard checks on end(); lifetime users retain positive checks
// only for the same VM base and protection epoch. No cache is an unmapping lease.
class ScopedCache {
    struct Page {
        std::uint64_t generation=0;
        std::uint32_t number=0;
        std::uint16_t readable=0,writable=0;
    };
    std::array<Page,256> pages_{};
    struct Region {std::uint64_t generation=0;Mapping mapping;};
    std::array<Region,16> regions_{};
    unsigned next_region_=0;
    std::uintptr_t base_=0;
    std::uint64_t generation_=1;
    std::uint64_t lifetime_epoch_=0;
    bool active_=false;
    void next_generation()noexcept {
        if(++generation_==0){for(auto& page:pages_)page={};for(auto& region:regions_)region={};generation_=1;}
    }
    static std::uint16_t mask(std::uint64_t first,std::uint64_t end)noexcept {
        const auto low=unsigned(first>>12),high=unsigned((end+4095u)>>12);
        return std::uint16_t(((std::uint32_t{1}<<high)-1u)&~((std::uint32_t{1}<<low)-1u));
    }
public:
    void begin(const void* base)noexcept {next_generation();base_=reinterpret_cast<std::uintptr_t>(base);active_=true;}
    // Retain positive mapping checks across jobs only while the runtime's VM
    // lifetime/protection epoch is unchanged. Epoch0 disables cross-job reuse.
    void begin_lifetime(const void* base,std::uint64_t epoch)noexcept {
        const auto host=reinterpret_cast<std::uintptr_t>(base);
        if(!epoch||host!=base_||epoch!=lifetime_epoch_)next_generation();
        base_=host;lifetime_epoch_=epoch;active_=true;
    }
    void suspend()noexcept {active_=false;}
    void end()noexcept {next_generation();active_=false;base_=0;lifetime_epoch_=0;}
    void invalidate()noexcept {next_generation();}
    bool active()const noexcept{return active_;}
    template<class Backend>
    bool check(const void* base,std::uint32_t ea,std::size_t bytes,bool writing,Backend& backend)noexcept {
        if(!base||!bytes||std::uint64_t(ea)+bytes>0x100000000ull)return false;
        const auto host=reinterpret_cast<std::uintptr_t>(base);
        if(host>UINTPTR_MAX-ea||bytes>UINTPTR_MAX-host-ea)return false;
        const bool caching=active_&&base_==host;
        const auto end=std::uint64_t(ea)+bytes;
        auto cursor=std::uint64_t(ea);
        while(cursor<end) {
            const auto page_number=std::uint32_t(cursor>>16);
            const auto page_begin=std::uint64_t(page_number)<<16;
            const auto page_end=page_begin+65536u;
            const auto requested_end=(std::min)(end,page_end);
            auto& page=pages_[page_number%pages_.size()];
            if(caching) {
                if(page.generation!=generation_||page.number!=page_number)
                    page={generation_,page_number,0,0};
                const auto required=mask(cursor-page_begin,requested_end-page_begin);
                const auto present=writing?page.writable:page.readable;
                if((present&required)==required){cursor=requested_end;continue;}
            }
            Mapping mapping;
            bool found=false;
            if(caching)for(const auto& region:regions_)
                if(region.generation==generation_&&region.mapping.begin<=cursor&&cursor<region.mapping.end) {
                    mapping=region.mapping;found=true;break;
                }
            if((!found&&!backend.query(std::uint32_t(cursor),mapping))||!mapping.own_reservation||
               mapping.begin>cursor||mapping.end<=cursor||mapping.end>0x100000000ull)return false;
            if(mapping.reserved) {
                if(!backend.commit(std::uint32_t(page_begin))||
                   !backend.query(std::uint32_t(cursor),mapping)||!mapping.own_reservation||
                   mapping.begin>cursor||mapping.end<=cursor||mapping.end>0x100000000ull)return false;
            }
            if(!mapping.committed||!mapping.readable||(writing&&!mapping.writable))return false;
            if(caching) {
                if(!found)regions_[next_region_++%regions_.size()]={generation_,mapping};
                // Only complete4KiB host pages inside this exact uniform
                // VirtualQuery region gain permission; no first/last shortcut
                // can accidentally admit an intervening guard/protected page.
                const auto full_begin=((std::max)(page_begin,mapping.begin)+4095u)&~std::uint64_t(4095u);
                const auto full_end=(std::min)(page_end,mapping.end)&~std::uint64_t(4095u);
                if(full_begin<full_end) {
                    const auto valid=mask(full_begin-page_begin,full_end-page_begin);
                    page.readable|=valid;if(mapping.writable)page.writable|=valid;
                }
            }
            cursor=(std::min)(requested_end,mapping.end);
        }
        return true;
    }
};

#ifdef _WIN32
struct WindowsBackend {
    std::uint8_t* base;
    unsigned queries=0,commits=0;
    bool query(std::uint32_t ea,Mapping& output)noexcept {
        ++queries;MEMORY_BASIC_INFORMATION info{};
        if(VirtualQuery(base+ea,&info,sizeof(info))!=sizeof(info))return false;
        const auto allocation=reinterpret_cast<std::uintptr_t>(base);
        const auto begin=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(begin<allocation||info.RegionSize>UINTPTR_MAX-begin)return false;
        const auto protection=info.Protect&0xffu;
        const bool guarded=(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))!=0;
        const bool readable=!guarded&&(protection==PAGE_READONLY||protection==PAGE_READWRITE||
            protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READ||
            protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY);
        const bool writable=!guarded&&(protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||
            protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY);
        output={std::uint64_t(begin-allocation),std::uint64_t(begin-allocation)+info.RegionSize,
            info.AllocationBase==base,info.State==MEM_COMMIT,info.State==MEM_RESERVE,readable,writable};
        return true;
    }
    bool commit(std::uint32_t page)noexcept {
        ++commits;return VirtualAlloc(base+page,65536,MEM_COMMIT,PAGE_READWRITE)!=nullptr;
    }
};
#endif
}
#endif
