#include "tcache.h"
#include "codegen/jitabi.h"

namespace dbt
{
tcache::L1Cache tcache::l1_cache{};
tcache::L1BrindCache tcache::l1_brind_cache{};
tcache::MapType tcache::tcache_map{};
MemArena tcache::code_pool{};
MemArena tcache::tb_pool{};
std::multimap<u32, jitabi::ppoint::BranchSlot *> tcache::link_map;

void tcache::Init()
{
    l1_cache.fill(nullptr);
    l1_brind_cache.fill({0, nullptr});
    tcache_map.clear();
    tb_pool.Init(TB_POOL_SIZE, PROT_READ | PROT_WRITE);
    code_pool.Init(CODE_POOL_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void tcache::Destroy()
{
    l1_cache.fill(nullptr);
    l1_brind_cache.fill({0, nullptr});
    tcache_map.clear();
    tb_pool.Destroy();
    code_pool.Destroy();
}

void tcache::Invalidate()
{
    l1_cache.fill(nullptr);
    l1_brind_cache.fill({0, nullptr});
    tcache_map.clear();
    tb_pool.Reset();
    code_pool.Reset();
    link_map.clear();
}

void tcache::InvalidatePage(u32 pvaddr)
{
    assert(rounddown(pvaddr, mmu::PAGE_SIZE) == pvaddr);
    for (auto it = link_map.upper_bound(pvaddr);
         it->first < pvaddr + mmu::PAGE_SIZE;) {
        it->second->LinkLazyJIT();
        it = link_map.erase(it);
    }
    for (auto it = tcache_map.upper_bound(pvaddr);
         it->first < pvaddr + mmu::PAGE_SIZE;) {
        it = tcache_map.erase(it);
    }
    for (auto &e : l1_cache) {
        if (rounddown(e->ip, mmu::PAGE_SIZE) == pvaddr)
            e = nullptr;
    }
    for (auto &e : l1_brind_cache) {
        if (rounddown(e.gip, mmu::PAGE_SIZE) == pvaddr)
            e = {0, 0};
    }
}

void tcache::Insert(TBlock *tb)
{
    tcache_map.insert({tb->ip, tb});
    l1_cache[l1hash(tb->ip)] = tb;
}

TBlock *tcache::LookupUpperBound(u32 gip)
{
    auto it = tcache_map.upper_bound(gip);
    if (it == tcache_map.end())
        return nullptr;
    return it->second;
}

TBlock *tcache::LookupFull(u32 gip)
{
    auto it = tcache_map.find(gip);
    if (likely(it != tcache_map.end()))
        return it->second;
    return nullptr;
}

TBlock *tcache::AllocateTBlock()
{
    auto *res = tb_pool.Allocate<TBlock>();
    if (res == nullptr)
        Invalidate();
    return new (res) TBlock{};
}

void *tcache::AllocateCode(size_t code_sz, u16 align)
{
    void *res = code_pool.Allocate(code_sz, align);
    if (res == nullptr)
        Invalidate();
    return res;
}

}  // namespace dbt
