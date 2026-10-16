#include "sys_memory.h"
#include <assert.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;
uint32_t ppu_hle_inject_base = 0x20000000;

static uint32_t read_be32(uint32_t addr)
{
    const uint8_t* p = vm_base + addr;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static uint32_t call(lv2_syscall_table* table, uint32_t number,
                     uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    ppu_context ctx = {0};
    ctx.gpr[3] = a; ctx.gpr[4] = b; ctx.gpr[5] = c; ctx.gpr[6] = d;
    assert(table->handlers[number]);
    return (uint32_t)table->handlers[number](&ctx);
}

int main(void)
{
    lv2_syscall_table table = {0};
    assert(vm_init() == CELL_OK);
    sys_memory_init(&table);
    const uint32_t output = 0x10000;
    assert(call(&table, SYS_MEMORY_CONTAINER_CREATE, output, 0x1800000, 0, 0) == CELL_OK);
    uint32_t cid = read_be32(output);
    assert(call(&table, SYS_MEMORY_GET_USER_MEMORY_SIZE, output + 16, 0, 0, 0) == CELL_OK);
    uint32_t main_available = read_be32(output + 20);
    assert(main_available == 213u * 1024 * 1024 - 0x1800000);
    /* WKC2's exact syscall 350 argument order and 24 MB request. */
    assert(call(&table, SYS_MEMORY_ALLOCATE_FROM_CONTAINER, 0x1800000, cid, 0x200, output + 4) == CELL_OK);
    uint32_t allocation = read_be32(output + 4);
    assert(allocation && !(allocation & 0xFFFF));
    vm_base[allocation] = 1;
    vm_base[allocation + 0x17FFFFF] = 2;
    assert(call(&table, SYS_MEMORY_CONTAINER_GET_SIZE, output + 8, cid, 0, 0) == CELL_OK);
    assert(read_be32(output + 8) == 0x1800000 && read_be32(output + 12) == 0);
    assert(call(&table, SYS_MEMORY_GET_USER_MEMORY_SIZE, output + 16, 0, 0, 0) == CELL_OK);
    assert(read_be32(output + 20) == main_available); /* Container isn't charged twice. */
    assert(call(&table, SYS_MEMORY_ALLOCATE_FROM_CONTAINER, 0x10000, cid, 0x200, output + 4) == CELL_ENOMEM);
    assert(read_be32(output + 4) == allocation);
    assert(call(&table, SYS_MEMORY_CONTAINER_DESTROY, cid, 0, 0, 0) == CELL_EBUSY);
    assert(call(&table, SYS_MEMORY_FREE, allocation, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_CONTAINER_GET_SIZE, output + 8, cid, 0, 0) == CELL_OK);
    assert(read_be32(output + 12) == 0x1800000);
    assert(call(&table, SYS_MEMORY_ALLOCATE_FROM_CONTAINER, 0x771, cid, 0x200, output + 4) == CELL_EALIGN);
    assert(call(&table, SYS_MEMORY_ALLOCATE_FROM_CONTAINER, 0x10000, cid, 0x800, output + 4) == CELL_EINVAL);
    assert(call(&table, SYS_MEMORY_CONTAINER_DESTROY, cid, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_GET_USER_MEMORY_SIZE, output + 16, 0, 0, 0) == CELL_OK);
    assert(read_be32(output + 20) == 213u * 1024 * 1024);
    assert(call(&table, SYS_MEMORY_ALLOCATE_FROM_CONTAINER, 0x10000, cid, 0x200, output + 4) == CELL_ESRCH);
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x100000, 0x400, output + 4, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_FREE, read_be32(output + 4), 0, 0, 0) == CELL_OK);

    /* Freed ranges are reused: repeated large allocate/free cycles, as on
     * scene changes, must not exhaust the 256 MB allocation window. */
    for (int cycle = 0; cycle < 8; ++cycle) {
        assert(call(&table, SYS_MEMORY_ALLOCATE, 0x6000000, 0x400, output + 4, 0) == CELL_OK);
        uint32_t block = read_be32(output + 4);
        vm_base[block] = 0xAA;
        vm_base[block + 0x5FFFFFF] = 0xBB;
        assert(call(&table, SYS_MEMORY_FREE, block, 0, 0, 0) == CELL_OK);
    }

    /* A reused range reads as zero, like a fresh commit. */
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x6000000, 0x400, output + 4, 0) == CELL_OK);
    uint32_t reused = read_be32(output + 4);
    assert(vm_base[reused] == 0 && vm_base[reused + 0x5FFFFFF] == 0);

    /* A gap between live allocations is filled, aligned, without overlap. */
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x100000, 0x400, output + 8, 0) == CELL_OK);
    uint32_t tail = read_be32(output + 8);
    assert(tail >= reused + 0x6000000);
    assert(call(&table, SYS_MEMORY_FREE, reused, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x10000, 0x200, output + 4, 0) == CELL_OK);
    uint32_t small = read_be32(output + 4);
    assert(small == reused && !(small & 0xFFFF));
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x100000, 0x400, output + 12, 0) == CELL_OK);
    uint32_t aligned = read_be32(output + 12);
    assert(!(aligned & 0xFFFFF) && aligned >= small + 0x10000 && aligned + 0x100000 <= tail);
    assert(call(&table, SYS_MEMORY_FREE, small, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_FREE, aligned, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_FREE, tail, 0, 0, 0) == CELL_OK);
    /* Reuse must also respect ranges owned by other users of this window. */
    assert(call(&table, SYS_MMAPPER_ALLOCATE_ADDRESS, 0x200000, 0, 0x100000, output + 16) == CELL_OK);
    uint32_t reserved = read_be32(output + 16);
    vm_base[reserved] = 0xC1;
    assert(call(&table, SYS_MEMORY_ALLOCATE, reserved - 0x40000000, 0x200, output + 4, 0) == CELL_OK);
    uint32_t prefix = read_be32(output + 4);
    assert(prefix == 0x40000000);
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x10000, 0x200, output + 8, 0) == CELL_OK);
    uint32_t after_reservation = read_be32(output + 8);
    assert(after_reservation >= reserved + 0x200000);
    assert(vm_base[reserved] == 0xC1);
    assert(call(&table, 341, output + 20, 0x300000, 0, 0) == CELL_OK);
    uint32_t mapped = read_be32(output + 20);
    vm_base[mapped] = 0xC2;
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x10000, 0x200, output + 12, 0) == CELL_OK);
    uint32_t after_mapping = read_be32(output + 12);
    assert(after_mapping >= mapped + 0x300000);
    assert(vm_base[mapped] == 0xC2);
    assert(call(&table, SYS_MMAPPER_ALLOCATE_SHARED_MEMORY, 0, 0x100000, 0, output + 24) == CELL_OK);
    uint32_t shared_id = read_be32(output + 24);
    uint32_t shared_address = after_mapping + 0x10000;
    assert(call(&table, SYS_MMAPPER_MAP_SHARED_MEMORY, shared_address, shared_id, 0, 0) == CELL_OK);
    vm_base[shared_address] = 0xC3;
    assert(call(&table, SYS_MEMORY_ALLOCATE, 0x10000, 0x200, output + 28, 0) == CELL_OK);
    uint32_t after_shared = read_be32(output + 28);
    assert(after_shared >= shared_address + 0x100000);
    assert(vm_base[shared_address] == 0xC3);
    assert(call(&table, SYS_MEMORY_FREE, prefix, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_FREE, after_reservation, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_FREE, after_mapping, 0, 0, 0) == CELL_OK);
    assert(call(&table, SYS_MEMORY_FREE, after_shared, 0, 0, 0) == CELL_OK);
    vm_shutdown();
    return 0;
}
