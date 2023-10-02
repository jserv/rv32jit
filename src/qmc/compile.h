#pragma once

#include <span>
#include <vector>

#include "arena.h"
#include "util/common.h"

namespace dbt
{
using IpRange = std::pair<u32, u32>;

struct CompilerRuntime {
    virtual void *AllocateCode(size_t sz, uint align) = 0;

    virtual bool AllowsRelocation() const = 0;

    virtual void *AnnounceRegion(u32 ip, std::span<u8> const &code) = 0;
};
}  // namespace dbt

namespace dbt::qir
{
struct CodeSegment {
    explicit CodeSegment(u32 gip_base_, u32 size_)
        : gip_base(gip_base_), size(size_)
    {
    }

    bool InSegment(u32 gip) const { return (gip - gip_base) < size; }

    u32 gip_base;
    u32 size;
};

struct CompilerJob {
    using IpRangesSet = std::vector<IpRange>;

    explicit CompilerJob(CompilerRuntime *cruntime_,
                         uptr vmem_,
                         CodeSegment segment_,
                         IpRangesSet &&iprange_)
        : cruntime(cruntime_), vmem(vmem_), segment(segment_), iprange(iprange_)
    {
        assert(iprange.size());
    }

    CompilerRuntime *cruntime;

    uptr vmem;
    CodeSegment segment;
    IpRangesSet iprange;
};

// Now qmc operates only in synchronous mode, so returns a value from
// runtime.AnnounceRegion
void *CompilerDoJob(CompilerJob &job);

struct Region;
// Just generate IR
Region *CompilerGenRegionIR(MemArena *arena, CompilerJob &job);

}  // namespace dbt::qir
