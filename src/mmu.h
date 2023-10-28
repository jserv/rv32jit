#pragma once

#include <sys/mman.h>
#include <bitset>
#include <cstdint>
#include <unordered_map>

#include "util/allocator.h"

namespace dbt
{
struct mmu {
    static constexpr size_t ASPACE_SIZE = (1ull) << 32;
    static constexpr size_t PAGE_BITS = 12;  // true for rv32 and amd64
    static constexpr size_t PAGE_SIZE = 1 << PAGE_BITS;
    static constexpr size_t PAGE_MASK = ~(PAGE_SIZE - 1);
    static constexpr size_t MIN_MMAP_ADDR = 16 * PAGE_SIZE;
    static void Init();
    static void Destroy();
    static void *mmap(u32 vaddr,
                      u32 len,
                      int prot,
                      int flag = MAP_ANON | MAP_PRIVATE | MAP_FIXED,
                      int fd = -1,
                      size_t offs = 0);

    static ALWAYS_INLINE bool check_h2g(void *hptr)
    {
        return ((uptr) hptr - (uptr) base) < ASPACE_SIZE;
    }

    // host to guest
    static ALWAYS_INLINE u32 h2g(void *hptr)
    {
        return (uptr) hptr - (uptr) base;
    }

    // guest to host
    static ALWAYS_INLINE void *g2h(u32 gptr) { return base + gptr; }

    static u8 *base;

private:
    static std::bitset<(ASPACE_SIZE >> PAGE_BITS)> used_pages;
    static u32 mmap_hint_page;

    static void MarkUsedPages(u32 pvaddr, u32 plen);
    static void MarkFreePages(u32 pvaddr, u32 plen);
    static u32 LookupFreeRange(u32 pvaddr, u32 plen);

    mmu() = delete;
};

}  // namespace dbt
