#pragma once

#include "qmc/compile.h"
#include "qmc/qir.h"

namespace dbt::qcg
{
std::span<u8> GenerateCode(CompilerRuntime *cruntime,
                           qir::CodeSegment *segment,
                           qir::Region *r,
                           u32 ip);

struct MachineRegionInfo {
    bool has_calls = false;
};

struct QSelPass {
    static void run(qir::Region *region, MachineRegionInfo *region_info);
};

struct QRegAllocPass {
    static void run(qir::Region *region);
};

}  // namespace dbt::qcg
