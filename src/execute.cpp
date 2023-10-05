#include "execute.h"
#include "codegen/jitabi.h"
#include "guest/rv32_cpu.h"
#include "guest/rv32_ops.h"
#include "ir/compile.h"

namespace dbt
{
sigjmp_buf trap_unwind_env;

static inline bool HandleTrap(CPUState *state)
{
    // Currenlty only delegates to env
    return !state->IsTrapPending();
}

struct JITCompilerRuntime final : CompilerRuntime {
    void *AllocateCode(size_t sz, uint align) override
    {
        return tcache::AllocateCode(sz, align);
    }

    bool AllowsRelocation() const override { return false; }

    void *AnnounceRegion(u32 ip, std::span<u8> const &code) override
    {
        // TODO: concurrent tcache
        auto tb = tcache::AllocateTBlock();
        if (tb == nullptr)
            Panic();
        tb->ip = ip;
        tb->tcode = TBlock::TCode{code.data(), code.size()};
        tcache::Insert(tb);
        return (void *) tb;
    }
};

static inline IpRange GetCompilationIPRange(u32 ip)
{
    u32 upper = roundup(ip, mmu::PAGE_SIZE);
    if (auto *tb_upper = tcache::LookupUpperBound(ip))
        upper = std::min(upper, tb_upper->ip);
    return {ip, upper};
}

void Execute(CPUState *state)
{
    sigsetjmp(dbt::trap_unwind_env, 0);

    jitabi::ppoint::BranchSlot *branch_slot = nullptr;

    while (likely(!HandleTrap(state))) {
        assert(state == CPUState::Current());
        assert(state->gpr[0] == 0);
        assert(!branch_slot || branch_slot->gip == state->ip);

        TBlock *tb = tcache::Lookup(state->ip);
        if (tb == nullptr) {
            auto jrt = JITCompilerRuntime();
            u32 gip_page = rounddown(state->ip, mmu::PAGE_SIZE);
            qir::CompilerJob job(&jrt, (uptr) mmu::base,
                                 qir::CodeSegment(gip_page, mmu::PAGE_SIZE),
                                 {GetCompilationIPRange(state->ip)});
            tb = (TBlock *) qir::CompilerDoJob(job);
        }

        if (branch_slot) {
            branch_slot->Link(tb->tcode.ptr);
            tcache::RecordLink(branch_slot, tb,
                               branch_slot->flags.cross_segment);
        } else {
            tcache::CacheBrind(tb);
        }

        branch_slot =
            jitabi::trampoline_to_jit(state, mmu::base, tb->tcode.ptr);
    }
}

}  // namespace dbt
