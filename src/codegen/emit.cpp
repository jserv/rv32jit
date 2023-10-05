#include "codegen/emit.h"
#include "guest/rv32_cpu.h"

namespace dbt::qcg
{
QEmit::QEmit(qir::Region *region,
             CompilerRuntime *cruntime_,
             qir::CodeSegment *segment_,
             bool is_leaf_)
    : cruntime(cruntime_), segment(segment_), is_leaf(is_leaf_)
{
    spillframe_sp_offs = sizeof(uptr) * (is_leaf ? 1 : 2);

    if (jcode.init(jrt.environment()))
        Panic();

    jcode.attach(j._emitter());
    j.setErrorHandler(&jerr);
    jcode.setErrorHandler(&jerr);

    u32 n_labels = region->GetNumBlocks();
    labels.reserve(n_labels);
    for (u32 i = 0; i < n_labels; ++i)
        labels.push_back(j.newLabel());
}

std::span<u8> QEmit::EmitCode()
{
    jcode.flatten();
    jcode.resolveUnresolvedLinks();

    size_t code_sz = jcode.codeSize();
    void *code_ptr = cruntime->AllocateCode(code_sz, 8);
    if (code_ptr == nullptr)
        Panic();

    jcode.relocateToBase((uptr) code_ptr);
    jcode.copyFlattenedData(code_ptr, code_sz);
    code_sz = jcode.codeSize();
    return {(u8 *) code_ptr, code_sz};
}

static inline asmjit::x86::Gp make_gpr(qir::RegN pr, qir::VType type)
{
    switch (type) {
    case qir::VType::I8:
        return asmjit::x86::gpb(pr);
    case qir::VType::I16:
        return asmjit::x86::gpw(pr);
    case qir::VType::I32:
        return asmjit::x86::gpd(pr);
    default:
        unreachable("");
    }
}

static inline asmjit::x86::Gp make_gpr(qir::VOperand opr)
{
    return make_gpr(opr.GetPGPR(), opr.GetType());
}

static inline asmjit::Imm make_imm(qir::VOperand opr)
{
    return asmjit::imm(opr.GetConst());
}

inline asmjit::x86::Mem QEmit::make_slot(qir::VOperand opr)
{
    auto size = VTypeToSize(opr.GetType());
    auto offs = opr.GetSlotOffs();
    asmjit::x86::Gp base;
    if (opr.IsLSlot()) {
        base = QEmit::R_SP;
        offs += spillframe_sp_offs;
    } else {
        base = QEmit::R_STATE;
    }
    return asmjit::x86::Mem(base, offs, size);
}

inline asmjit::Operand QEmit::make_operand(qir::VOperand opr)
{
    if (likely(opr.IsGPR()))
        return make_gpr(opr);
    if (opr.IsConst())
        return make_imm(opr);
    return make_slot(opr);
}

inline asmjit::Operand QEmit::make_stubcall_target(RuntimeStubId stub)
{
    return asmjit::imm(stub_tab[stub]);
}

static inline asmjit::x86::CondCode make_cc(qir::CondCode cc)
{
    switch (cc) {
    case qir::CondCode::EQ:
        return asmjit::x86::CondCode::kEqual;
    case qir::CondCode::NE:
        return asmjit::x86::CondCode::kNotEqual;
    case qir::CondCode::LE:
        return asmjit::x86::CondCode::kSignedLE;
    case qir::CondCode::LT:
        return asmjit::x86::CondCode::kSignedLT;
    case qir::CondCode::GE:
        return asmjit::x86::CondCode::kSignedGE;
    case qir::CondCode::GT:
        return asmjit::x86::CondCode::kSignedGT;
    case qir::CondCode::LEU:
        return asmjit::x86::CondCode::kUnsignedLE;
    case qir::CondCode::LTU:
        return asmjit::x86::CondCode::kUnsignedLT;
    case qir::CondCode::GEU:
        return asmjit::x86::CondCode::kUnsignedGE;
    case qir::CondCode::GTU:
        return asmjit::x86::CondCode::kUnsignedGT;
    default:
        unreachable("");
    }
}

void QEmit::FrameSetup()
{
    if (!is_leaf) {
        // Push something to satisfy x86 frame alignment
        j.push(asmjit::x86::rcx);
    }
}

void QEmit::FrameDestroy()
{
    if (!is_leaf)
        j.pop(asmjit::x86::rcx);
}

void QEmit::Prologue(UNUSED u32 ip)
{
    FrameSetup();
}

void QEmit::StateFill(qir::RegN p, qir::VType type, u16 offs)
{
    auto slot = asmjit::x86::ptr(R_STATE, offs);
    slot.setSize(VTypeToSize(type));
    j.mov(make_gpr(p, type), slot);
}

void QEmit::StateSpill(qir::RegN p, qir::VType type, u16 offs)
{
    auto slot = asmjit::x86::ptr(R_STATE, offs);
    slot.setSize(VTypeToSize(type));
    j.mov(slot, make_gpr(p, type));
}

void QEmit::LocFill(qir::RegN p, qir::VType type, u16 offs)
{
    auto slot = asmjit::x86::ptr(R_SP, offs);
    slot.setSize(VTypeToSize(type));
    j.mov(make_gpr(p, type), slot);
}

void QEmit::LocSpill(qir::RegN p, qir::VType type, u16 offs)
{
    auto slot = asmjit::x86::ptr(R_SP, offs);
    slot.setSize(VTypeToSize(type));
    j.mov(slot, make_gpr(p, type));
}

void QEmit::Emit_hcall(qir::InstHcall *ins)
{
    assert(!is_leaf);
    j.mov(asmjit::x86::rdi, R_STATE);
    j.emit(asmjit::x86::Inst::kIdMov, asmjit::x86::rsi,
           make_operand(ins->i(0)));
    j.emit(asmjit::x86::Inst::kIdCall, make_stubcall_target(ins->stub));
}

void QEmit::Emit_br(UNUSED qir::InstBr *ins)
{
    auto bb_s = bb->GetSuccs().at(0);
    auto bb_ff = &*++bb->getIter();
    if (bb_s != bb_ff)
        j.jmp(labels[bb_s->GetId()]);
}

void QEmit::Emit_brcc(qir::InstBrcc *ins)
{
    auto bb_t = bb->GetSuccs().at(0);
    auto bb_f = bb->GetSuccs().at(1);
    auto bb_ff = &*++bb->getIter();

    auto &vs0 = ins->i(0);
    auto &vs1 = ins->i(1);

    // constfolded
    if (vs0.IsConst()) {
        std::swap(vs0, vs1);
        ins->cc = qir::SwapCC(ins->cc);
    }
    auto cc = ins->cc;

    j.emit(asmjit::x86::Inst::kIdCmp, make_operand(vs0), make_operand(vs1));
    auto jcc = asmjit::x86::Inst::jccFromCond(make_cc(cc));
    j.emit(jcc, labels[bb_t->GetId()]);

    if (bb_f != bb_ff)
        j.jmp(labels[bb_f->GetId()]);
}

void QEmit::Emit_gbr(qir::InstGBr *ins)
{
    FrameDestroy();
    static constexpr size_t patch_size = sizeof(jitabi::ppoint::BranchSlot);
    j.embedUInt8(0, patch_size);
    auto *slot = (jitabi::ppoint::BranchSlot *) (j.bufferPtr() - patch_size);
    slot->gip = ins->tpc.GetConst();
    slot->flags.cross_segment = !segment->InSegment(slot->gip);
    slot->LinkLazyJIT();
}

void QEmit::Emit_gbrind(qir::InstGBrind *ins)
{
    auto ptgt = make_gpr(ins->i(0));
    assert(ptgt.id() == asmjit::x86::Gp::kIdSi);

    auto slowpath = j.newLabel();
    {
        // Inlined l1_brind_cache lookup
        auto tmp0 = asmjit::x86::rdi;
        auto tmp1 = asmjit::x86::rdx;
        j.mov(tmp1.r64(), (uptr) tcache::l1_brind_cache.data());

        static_assert(sizeof(tcache::BrindCacheEntry) == 1u << 4);
        static_assert(offsetof(tcache::BrindCacheEntry, gip) == 0);

        j.lea(tmp0.r32(), asmjit::x86::ptr(0, ptgt.r64(), 2));
        j.and_(tmp0.r32(), ((1ull << tcache::L1_CACHE_BITS) - 1) << 4);

        j.cmp(asmjit::x86::ptr(tmp1.r64(), tmp0.r64(), 0, 0, sizeof(u32)),
              ptgt.r32());
        j.jne(slowpath);

        FrameDestroy();
        j.jmp(asmjit::x86::ptr(tmp1.r64(), tmp0.r64(), 0,
                               offsetof(tcache::BrindCacheEntry, code),
                               sizeof(u64)));
    }

    j.bind(slowpath);

    j.mov(asmjit::x86::gpq(asmjit::x86::Gp::kIdDi), R_STATE);

    // Allow call in leaf procedure and setup frame in slowpath
    if (is_leaf)
        j.push(asmjit::x86::rcx);

    j.emit(asmjit::x86::Inst::kIdCall,
           make_stubcall_target(RuntimeStubId::id_brind));

    if (is_leaf) {
        j.pop(asmjit::x86::rcx);
    } else {
        FrameDestroy();
    }
    j.jmp(asmjit::x86::rax);
}

// set size manually
static inline asmjit::x86::Mem make_vmem(qir::VOperand vbase)
{
#if CONFIG_ZERO_MMU_BASE
    if (likely(vbase.IsPGPR()))
        return asmjit::x86::ptr(make_gpr(vbase));
    return asmjit::x86::ptr(vbase.GetConst());
#else
    if (likely(vbase.IsPGPR()))
        return asmjit::x86::ptr(QEmit::R_MEMBASE, make_gpr(vbase));
    return asmjit::x86::ptr(QEmit::R_MEMBASE, vbase.GetConst());
#endif
}

void QEmit::Emit_vmload(qir::InstVMLoad *ins)
{
    auto &vrd = ins->o(0);
    auto &vbase = ins->i(0);
    auto sgn = ins->sgn;

    auto prd = make_gpr(vrd);
    auto mem = make_vmem(vbase);

    assert(vrd.GetType() == qir::VType::I32);
    switch (ins->sz) {
    case qir::VType::I8:
        mem.setSize(1);
        if (sgn == qir::VSign::U) {
            j.movzx(prd, mem);
        } else {
            j.movsx(prd, mem);
        }
        break;
    case qir::VType::I16:
        mem.setSize(2);
        if (sgn == qir::VSign::U) {
            j.movzx(prd, mem);
        } else {
            j.movsx(prd, mem);
        }
        break;
    case qir::VType::I32:
        mem.setSize(4);
        j.mov(prd, mem);
        break;
    default:
        unreachable("");
    };
}

void QEmit::Emit_vmstore(qir::InstVMStore *ins)
{
    auto &vbase = ins->i(0);
    auto &vdata = ins->i(1);

    auto pdata = make_operand(vdata);
    auto mem = make_vmem(vbase);

    assert(ins->sgn == qir::VSign::U);
    mem.setSize(VTypeToSize(ins->sz));
    j.emit(asmjit::x86::Inst::kIdMov, mem, pdata);
}

void QEmit::Emit_setcc(qir::InstSetcc *ins)
{
    auto prd = make_gpr(ins->o(0));
    auto vs0 = ins->i(0);
    auto vs1 = ins->i(1);
    auto cc = ins->cc;

    bool dst_aliased = vs0.GetPGPR() == prd.id() ||
                       (vs1.IsPGPR() && vs1.GetPGPR() == prd.id());

    if (!dst_aliased)
        j.xor_(prd, prd);

    j.emit(asmjit::x86::Inst::kIdCmp, make_operand(vs0), make_operand(vs1));
    auto setcc = asmjit::x86::Inst::setccFromCond(make_cc(cc));
    j.emit(setcc, prd.r8());

    if (dst_aliased)
        j.movzx(prd, prd.r8());
}

void QEmit::Emit_mov(qir::InstUnop *ins)
{
    auto vrd = ins->o(0);
    auto vs0 = ins->i(0);
    // TODO: slowed code by ~3%, try again after bb merging
    if (unlikely(false && vs0.IsConst() && vs0.GetConst() == 0 &&
                 vrd.IsPGPR())) {
        auto prd = make_gpr(vrd);
        j.emit(asmjit::x86::Inst::kIdXor, prd, prd);
        return;
    }
    j.emit(asmjit::x86::Inst::kIdMov, make_operand(vrd), make_operand(vs0));
}

template <asmjit::x86::Inst::Id Op>
ALWAYS_INLINE void QEmit::EmitInstBinop(qir::InstBinop *ins)
{
    auto &vrd = ins->o(0);
    [[maybe_unused]] auto vs0 = ins->i(0);
    auto vs1 = ins->i(1);

    assert(vrd.GetPGPR() == vs0.GetPGPR());
    j.emit(Op, make_gpr(vrd), make_operand(vs1));
}

void QEmit::Emit_add(qir::InstBinop *ins)
{
    EmitInstBinop<asmjit::x86::Inst::kIdAdd>(ins);
}

void QEmit::Emit_sub(qir::InstBinop *ins)
{
    EmitInstBinop<asmjit::x86::Inst::kIdSub>(ins);
}

void QEmit::Emit_and(qir::InstBinop *ins)
{
    EmitInstBinop<asmjit::x86::Inst::kIdAnd>(ins);
}

void QEmit::Emit_or(qir::InstBinop *ins)
{
    EmitInstBinop<asmjit::x86::Inst::kIdOr>(ins);
}

void QEmit::Emit_xor(qir::InstBinop *ins)
{
    EmitInstBinop<asmjit::x86::Inst::kIdXor>(ins);
}

void QEmit::Emit_sra(qir::InstBinop *ins)
{
    [[maybe_unused]] auto vs1 = ins->i(1);
    assert(vs1.IsConst() || vs1.GetPGPR() == asmjit::x86::Gp::kIdCx);
    EmitInstBinop<asmjit::x86::Inst::kIdSar>(ins);
}

void QEmit::Emit_srl(qir::InstBinop *ins)
{
    [[maybe_unused]] auto vs1 = ins->i(1);
    assert(vs1.IsConst() || vs1.GetPGPR() == asmjit::x86::Gp::kIdCx);
    EmitInstBinop<asmjit::x86::Inst::kIdShr>(ins);
}

void QEmit::Emit_sll(qir::InstBinop *ins)
{
    [[maybe_unused]] auto vs1 = ins->i(1);
    assert(vs1.IsConst() || vs1.GetPGPR() == asmjit::x86::Gp::kIdCx);
    EmitInstBinop<asmjit::x86::Inst::kIdShl>(ins);
}

}  // namespace dbt::qcg
