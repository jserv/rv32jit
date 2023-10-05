#pragma once

#include "codegen/arch_traits.h"

namespace dbt
{
struct CPUState;
}

namespace dbt::jitabi
{
namespace ppoint
{
struct BranchSlot {
private:
    struct Call64Abs {
        u64 op_mov_imm_rax : 16 = 0xb848;
        u64 imm : 64;
        u64 op_call_rax : 16 = 0xd0ff;
    } __attribute__((packed));

    struct Jump64Abs {
        u64 op_mov_imm_rax : 16 = 0xb848;
        u64 imm : 64;
        u64 op_jmp_rax : 16 = 0xe0ff;
    } __attribute__((packed));

    struct Jump32Rel {
        u64 op_jmp_imm : 8 = 0xe9;
        u32 imm : 32;
    } __attribute__((packed));

    // QCG-format relocation
    struct CallTab {
        u64 op_call_mem_r13_imm : 24 = 0x95ff41;
        u32 imm : 32;
    } __attribute__((packed));
    static_assert(qcg::ArchTraits::STATE == asmjit::x86::Gp::kIdR13);

    union {
    private:
        Call64Abs x0;
        Jump64Abs x1;
        Jump32Rel x2;
        CallTab x3;
    } __attribute__((packed, may_alias)) code;

    template <typename P, typename... Args>
    P *CreatePatch(Args &&...args)
    {
        static_assert(sizeof(P) <= sizeof(code));
        return new (&code) P(args...);
    }

public:
    void Link(void *to);
    void LinkLazyJIT();

    // Calculate BranchSlot* from retaddr if call by ptr was used
    static BranchSlot *FromCallPtrRetaddr(void *ra)
    {
        return (BranchSlot *) ((uptr) ra - sizeof(Call64Abs));
    }

    // Calculate BranchSlot* from retaddr if RuntimeStub call was used
    static BranchSlot *FromCallRuntimeStubRetaddr(void *ra)
    {
        return (BranchSlot *) ((uptr) ra - sizeof(CallTab));
    }

    u32 gip;
    struct {
        bool cross_segment : 1 {false};
    } flags;
} __attribute__((packed));

inline void BranchSlot::LinkLazyJIT()
{
    CreatePatch<Call64Abs>()->imm = (uptr) (*RuntimeStubTab::GetGlobal())
        [RuntimeStubId::id_link_branch_jit];
}

inline void BranchSlot::Link(void *to)
{
    iptr rel = (iptr) to - ((iptr) &code + sizeof(Jump32Rel));
    if ((i32) rel == rel) {
        CreatePatch<Jump32Rel>()->imm = rel;
    } else {
        CreatePatch<Jump64Abs>()->imm = (uptr) to;
    }
}

}  // namespace ppoint

extern "C" ppoint::BranchSlot *trampoline_to_jit(CPUState *state,
                                                 void *vmem,
                                                 void *tc_ptr);

}  // namespace dbt::jitabi
