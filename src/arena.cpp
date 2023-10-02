#include "arena.h"
#include "util/allocator.h"

void MemArena::Init(size_t size, int prot)
{
    assert(!pool);
    pool_sz = roundup(size, 4096);
    used = 0;
    pool = (u8 *) dbt::host_mmap(NULL, pool_sz, prot, MAP_ANON | MAP_PRIVATE,
                                 -1, 0);
    if (pool == MAP_FAILED)
        dbt::Panic("MemArena::Init failed");
}

void MemArena::Destroy()
{
    if (!pool)
        return;

    int rc = munmap(pool, pool_sz);
    if (rc)
        dbt::Panic("MemArena::Destroy failed");

    pool = nullptr;
}
