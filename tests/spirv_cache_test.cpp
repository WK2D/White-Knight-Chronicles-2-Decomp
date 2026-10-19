#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rsx_spirv_cache.h"
#include <cassert>
#include <cstdio>

static bool fail_allocation;
static unsigned allocations, releases;
static void* allocate(size_t bytes) {
    if (fail_allocation) return nullptr;
    ++allocations; return std::malloc(bytes);
}
static void release(void* block) { ++releases; std::free(block); }

int main() {
    const uint32_t a[]={1,2,3}, b[]={4,5};
    {
        RsxSpirvCache cache(1024,2,allocate,release);
        assert(cache.insert(1,"abc",3,7,a,3));
        assert(cache.find(1,"abc",3,7)->data()[2]==3);
        assert(!cache.find(2,"abc",3,7));
        assert(!cache.find(1,"abd",3,7)); // Deliberate hash collision.
        assert(!cache.find(1,"ab",2,7));
        assert(cache.insert(1,"abd",3,7,b,2));
        assert(cache.find(1,"abc",3,7)); // Refresh LRU before eviction.
        assert(cache.insert(3,"x",1,9,b,2));
        assert(!cache.find(1,"abd",3,7));
        assert(cache.find(1,"abc",3,7));
        assert(cache.count()==2 && cache.evictions()==1);
        fail_allocation=true;
        assert(!cache.insert(1,"new",3,8,b,2));
        assert(cache.count()==2 && cache.evictions()==1);
        assert(cache.find(1,"abc",3,7));
        fail_allocation=false;
    }
    assert(allocations==releases);
    {
        RsxSpirvCache cache(20,100,allocate,release);
        assert(cache.insert(1,"abc",3,7,a,3)); // 4 bytes source + 12 output.
        assert(!cache.insert(1,"too-large",9,9,a,3));
        assert(cache.find(1,"abc",3,7));
        assert(cache.insert(1,"second",6,8,b,2));
        assert(cache.count()==1 && cache.bytes()==16 && cache.evictions()==1);
        assert(!cache.insert(1,"x",std::numeric_limits<size_t>::max(),8,b,2));
        assert(!cache.insert(1,"x",1,8,b,std::numeric_limits<size_t>::max()));
    }
    assert(allocations==releases);
    assert(RsxSpirvCache::hash("abc",3)!=RsxSpirvCache::hash("abd",3));
    puts("SPIR-V cache: exact identity, collisions, LRU, bounds and allocation failure pass");
}
