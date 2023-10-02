#include "qmc/qcg/jitabi.h"
#include "execute.h"
#include "qmc/qcg/arch_traits.h"
#include "tcache.h"

namespace dbt::jitabi
{
struct _RetPair {
    void *v0;
    void *v1;
};

#define HELPER extern "C" NOINLINE __attribute__((used))
#define HELPER_ASM extern "C" NOINLINE __attribute__((used, naked))

/*    qmc qcg frame layout, grows down
 *
 *			| ....		|  Execution loop
 *	trampoline call +---------------+-----------------------
 *			| link+fp|saved |  qcg spill frame, created in trampoline
 *			+---------------+  no callee saved regs expected
 *			| qcg locals	|  returning to this frame is not allowed
 *  	       tailcall +---------------+-----------------------
 *			| link+pad  	|  Translated region frame
 *			+---------------|  Destroyed on branch to next region
 *			| locals	|  qcg/ghccc callconv doesn't preserve fp
 *   abs/qcg-reloc call +---------------+-----------------------
 *			| ....		|  qcgstub_* frame
 */

static_assert((qcg::ArchTraits::spillframe_size & 15) == 0);
static_assert(qcg::ArchTraits::STATE == asmjit::x86::Gp::kIdR13);
static_assert(qcg::ArchTraits::MEMBASE == asmjit::x86::Gp::kIdBp);

// Build qcg spillframe and enter translated code
HELPER_ASM ppoint::BranchSlot *trampoline_to_jit(CPUState *state,
                                                 void *vmem,
                                                 void *tc_ptr)
{
    asm("pushq	%rbp\n\t"
        "pushq	%rbx\n\t"
        "pushq	%r12\n\t"
        "pushq	%r13\n\t"
        "pushq	%r14\n\t"
        "pushq	%r15\n\t"
        "movq 	%rdi, %r13\n\t"    // STATE
        "movq	%rsi, %rbp\n\t");  // MEMBASE
    asm("sub     	$%c0, %%rsp\n\t"
        :
        : "i"(qcg::ArchTraits::spillframe_size + 8));
    asm("leaq	-8(%rsp), %rsi\n\t");  // sp of qcg tailcall frame
    asm("movq	%%rsi, %c0(%%r13)\n\t"
        :
        : "i"(offsetof(CPUState, sp_unwindptr)));
    asm("callq	*%rdx\n\t"  // tc_ptr
        "int	$3");       // use escape/raise stub instead
}

// Escape from translated code, forward rax(slot) to caller
HELPER_ASM void qcgstub_escape_link()
{
    asm("addq   	$%c0, %%rsp"
        :
        : "i"(qcg::ArchTraits::spillframe_size + 16));
    asm("popq	%r15\n\t"
        "popq	%r14\n\t"
        "popq	%r13\n\t"
        "popq	%r12\n\t"
        "popq	%rbx\n\t"
        "popq	%rbp\n\t"
        "retq	\n\t");
}

// Escape from translated code, return nullptr(slot) to caller
HELPER_ASM void qcgstub_escape_brind()
{
    asm("addq   	$%c0, %%rsp"
        :
        : "i"(qcg::ArchTraits::spillframe_size + 16));
    asm("popq	%r15\n\t"
        "popq	%r14\n\t"
        "popq	%r13\n\t"
        "popq	%r12\n\t"
        "popq	%rbx\n\t"
        "popq	%rbp\n\t"
        "xorq	%rax, %rax\n\t"
        "retq	\n\t");
}

// Caller uses 2nd value in returned pair as jump target
static ALWAYS_INLINE _RetPair TryLinkBranch(CPUState *state,
                                            ppoint::BranchSlot *slot)
{
    auto found = tcache::Lookup(slot->gip);
    if (likely(found)) {
        slot->Link(found->tcode.ptr);
        tcache::RecordLink(slot, found, slot->flags.cross_segment);
        return {slot, found->tcode.ptr};
    }
    state->ip = slot->gip;
    return {slot, (void *) qcgstub_escape_link};
}

// Lazy region linking, absolute call target (jit mode)
HELPER_ASM void qcgstub_link_branch_jit()
{
    asm("movq	0(%rsp), %rsi\n\t"
        "movq	%r13, %rdi\n\t"
        "callq	qcg_TryLinkBranchJIT@plt\n\t"
        "popq	%rdi\n\t"  // pop somewhere
        "jmpq	*%rdx\n\t");
}

HELPER _RetPair qcg_TryLinkBranchJIT(CPUState *state, void *retaddr)
{
    return TryLinkBranch(state,
                         ppoint::BranchSlot::FromCallPtrRetaddr(retaddr));
}

// Indirect branch slowpath
HELPER void *qcgstub_brind(CPUState *state, u32 gip)
{
    state->ip = gip;
    auto *found = tcache::Lookup(gip);
    if (likely(found)) {
        tcache::CacheBrind(found);
        return (void *) found->tcode.ptr;
    }
    return (void *) qcgstub_escape_brind;
}

HELPER void qcgstub_raise()
{
    RaiseTrap();
}

static_assert(qcg::ArchTraits::STATE == asmjit::x86::Gp::kIdR13);

struct TraceRing {
    static constexpr u32 size = 64;

    struct Record {
        u32 gip;
    };

    void push(Record const &rec) { arr[head++ % size] = rec; }

    u32 head = 0;
    std::array<Record, size> arr{};
};

}  // namespace dbt::jitabi
