#include "qmc/qcg/arch_traits.h"
#include "qmc/qcg/qcg.h"
#include "qmc/qir_builder.h"

namespace dbt::qcg
{
struct QRegAlloc {
    static constexpr auto N_PREGS = ArchTraits::GPR_NUM;
    static constexpr auto PREGS_POOL = ArchTraits::GPR_POOL;
    static constexpr auto MAX_VREGS = 512;  // TODO: reuse temps

    struct RTrack {
        RTrack() {}
        NO_COPY(RTrack)
        NO_MOVE(RTrack)

        static constexpr auto NO_SPILL = static_cast<u16>(-1);

        qir::VType type{};
        bool is_global{};
        u16 spill_offs{NO_SPILL};

    private:
        friend QRegAlloc;

        enum class Location : u8 {
            DEAD,
            MEM,
            REG,
        };

        qir::RegN p{};
        Location loc{Location::DEAD};
        bool spill_synced{false};  // valid if loc is REG
    };

    QRegAlloc(qir::Region *region_);
    void Run();

    qir::RegN AllocPReg(RegMask desire, RegMask avoid);
    void EmitSpill(RTrack *v);
    void EmitFill(RTrack *v);
    void EmitMov(qir::VOperand pdst, qir::VOperand psrc);
    void Spill(qir::RegN p);
    void Spill(RTrack *v);
    void SyncSpill(RTrack *v);
    template <bool kill>
    void Release(RTrack *v);
    void AllocFrameSlot(RTrack *v);
    void Fill(RTrack *v, RegMask desire, RegMask avoid);

    RTrack *AddTrack();
    RTrack *AddTrackGlobal(qir::VType type, u16 state_offs);
    RTrack *AddTrackLocal(qir::VType type);

    void Prologue();
    void BlockBoundary();
    void RegionBoundary();

    void AllocOp(qir::Inst *ins);
    void CallOp(bool use_globals = true);

    static constexpr u16 frame_size{ArchTraits::spillframe_size};

    qir::Region *region{};
    qir::VRegsInfo const *vregs_info{};
    qir::Builder qb{nullptr};

    RegMask fixed{ArchTraits::GPR_FIXED};
    u16 frame_cur{0};

    u16 n_vregs{0};
    std::array<RTrack, MAX_VREGS> vregs{};
    std::array<RTrack *, N_PREGS> p2v{nullptr};
};

QRegAlloc::QRegAlloc(qir::Region *region_)
    : region(region_), vregs_info(region->GetVRegsInfo())
{
    auto n_globals = vregs_info->NumGlobals();
    auto n_all = vregs_info->NumAll();

    for (u16 i = 0; i < n_globals; ++i) {
        auto *gr = vregs_info->GetGlobalInfo(i);
        AddTrackGlobal(gr->type, gr->state_offs);
    }

    for (u16 i = n_globals; i < n_all; ++i) {
        auto type = vregs_info->GetLocalType(i);
        AddTrackLocal(type);
    }
}

qir::RegN QRegAlloc::AllocPReg(RegMask desire, RegMask avoid)
{
    RegMask target = desire & ~avoid;
    for (qir::RegN p = 0; p < N_PREGS; ++p) {
        if (!p2v[p] && target.Test(p))
            return p;
    }

    for (qir::RegN p = 0; p < N_PREGS; ++p) {
        if (target.Test(p)) {
            Spill(p);
            return p;
        }
    }
    Panic();
}

void QRegAlloc::EmitSpill(RTrack *v)
{
    if (!v->is_global && (v->spill_offs == RTrack::NO_SPILL))
        AllocFrameSlot(v);
    auto pgpr = qir::VOperand::MakePGPR(v->type, v->p);
    qb.Create_mov(qir::VOperand::MakeSlot(v->is_global, v->type, v->spill_offs),
                  pgpr);
}

void QRegAlloc::EmitFill(RTrack *v)
{
    assert(v->spill_offs != RTrack::NO_SPILL);
    auto pgpr = qir::VOperand::MakePGPR(v->type, v->p);
    qb.Create_mov(
        pgpr, qir::VOperand::MakeSlot(v->is_global, v->type, v->spill_offs));
}

void QRegAlloc::EmitMov(qir::VOperand dst, qir::VOperand src)
{
    qb.Create_mov(dst, src);
}

void QRegAlloc::Spill(qir::RegN p)
{
    RTrack *v = p2v[p];
    if (!v)
        return;
    Spill(v);
}

void QRegAlloc::Spill(RTrack *v)
{
    SyncSpill(v);
    Release<false>(v);
}

void QRegAlloc::SyncSpill(RTrack *v)
{
    if (v->spill_synced)  // or fixed
        return;

    switch (v->loc) {
    case RTrack::Location::MEM:
        return;
    case RTrack::Location::REG:
        EmitSpill(v);
        break;
    default:
        Panic();
    }
    v->spill_synced = true;
}

template <bool kill>
void QRegAlloc::Release(RTrack *v)
{
    bool release_reg = (v->loc == RTrack::Location::REG);
    if (v->is_global) {  // return if fixed
        v->loc = RTrack::Location::MEM;
    } else {
        v->loc = kill ? RTrack::Location::DEAD
                      : RTrack::Location::MEM;  // TODO: liveness
    }
    if (release_reg)
        p2v[v->p] = nullptr;
}

void QRegAlloc::AllocFrameSlot(RTrack *v)
{
    assert(v->spill_offs == RTrack::NO_SPILL);
    assert(!v->is_global);

    u16 slot_sz = qir::VTypeToSize(v->type);
    u16 slot_offs = roundup(frame_cur, slot_sz);
    if (slot_offs + slot_sz > frame_size)
        Panic();
    v->spill_offs = slot_offs;
    frame_cur = slot_offs + slot_sz;
}

void QRegAlloc::Fill(RTrack *v, RegMask desire, RegMask avoid)
{
    switch (v->loc) {
    case RTrack::Location::MEM:
        v->p = AllocPReg(desire, avoid);
        v->loc = RTrack::Location::REG;
        p2v[v->p] = v;
        v->spill_synced = true;
        EmitFill(v);
        return;
    case RTrack::Location::REG:
        return;
    default:
        Panic();
    }
}

QRegAlloc::RTrack *QRegAlloc::AddTrack()
{
    if (n_vregs == vregs.size())
        Panic();
    auto *v = &vregs[n_vregs++];
    return new (v) RTrack();
}

QRegAlloc::RTrack *QRegAlloc::AddTrackGlobal(qir::VType type, u16 state_offs)
{
    auto *v = AddTrack();
    v->is_global = true;
    v->type = type;
    v->spill_offs = state_offs;
    return v;
}

QRegAlloc::RTrack *QRegAlloc::AddTrackLocal(qir::VType type)
{
    auto *v = AddTrack();
    v->is_global = false;
    v->type = type;
    v->spill_offs = RTrack::NO_SPILL;
    return v;
}

void QRegAlloc::Prologue()
{
    for (qir::RegN i = 0; i < n_vregs; ++i) {
        auto *v = &vregs[i];

        if (v->is_global) {
            v->loc = RTrack::Location::MEM;
        } else {
            v->loc = RTrack::Location::DEAD;
        }
    }
}

void QRegAlloc::BlockBoundary()
{
    for (qir::RegN i = 0; i < n_vregs; ++i) {
        if (vregs[i].is_global)  // TODO: locals escape BB
            Spill(&vregs[i]);    // skip if fixed
    }
}

void QRegAlloc::RegionBoundary()
{
    for (qir::RegN i = 0; i < n_vregs; ++i) {
        auto vreg = &vregs[i];
        if (vreg->is_global) {
            Spill(vreg);
        } else {
            Release<false>(vreg);
        }
    }
}

void QRegAlloc::AllocOp(qir::Inst *ins)
{
    auto srcl = ins->inputs();
    auto dstl = ins->outputs();
    auto dst_n = dstl.size();

    auto &op_info = GetOpInfo(ins->GetOpcode());
    auto *op_ct = op_info.ra_ct;
    auto *op_order = op_info.ra_order;
    assert(op_ct);

    auto avoid = fixed;

    for (u8 i_ao = 0; i_ao < srcl.size(); ++i_ao) {
        u8 i = op_order[dst_n + i_ao];
        auto ct = op_ct[dst_n + i];

        auto opr = &srcl[i];
        if (!opr->IsVGPR())
            continue;
        auto src = &vregs[opr->GetVGPR()];
        Fill(src, ct.cr, avoid);
        auto p = src->p;
        if (!ct.cr.Test(p)) {
            p = AllocPReg(ct.cr, avoid);
            qb.Create_mov(qir::VOperand::MakePGPR(src->type, p),
                          qir::VOperand::MakePGPR(src->type, src->p));
            if constexpr (false) {  // TODO(tuning): different dep. distance,
                                    // check perf
                p2v[src->p] = nullptr;
                p2v[p] = src;
                src->p = p;
            }
        }

        avoid.Set(p);
        *opr = qir::VOperand::MakePGPR(opr->GetType(), p);
    }

    if (ins->GetFlags() & qir::Inst::Flags::SIDEEFF) {
        for (int i = 0; i < n_vregs; ++i) {
            auto *v = &vregs[i];
            if (v->is_global)
                SyncSpill(v);
        }
    }

    for (u8 i_ao = 0; i_ao < dstl.size(); ++i_ao) {
        u8 i = op_order[i_ao];
        auto ct = op_ct[i];

        auto opr = &dstl[i];
        if (!opr->IsVGPR())
            continue;
        auto dst = &vregs[opr->GetVGPR()];

        // TODO(tuning): forcefull renaming, check perf
        if constexpr (true) {
            if (ct.has_alias) {
                // QSel guarantees there will be the same VReg, so dst already
                // matches ct
            } else {
                auto p = AllocPReg(ct.cr, avoid);
                if (dst->loc == RTrack::Location::REG) {
                    p2v[dst->p] = nullptr;
                }
                dst->loc = RTrack::Location::REG;
                p2v[p] = dst;
                dst->p = p;
            }
        } else {
            if (dst->loc != RTrack::Location::REG) {
                dst->p = AllocPReg(ct.cr, avoid);
                p2v[dst->p] = dst;
                dst->loc = RTrack::Location::REG;
            } else if (!ct.cr.Test(dst->p)) {
                auto p = AllocPReg(ct.cr, avoid);
                p2v[dst->p] = nullptr;
                p2v[p] = dst;
                dst->p = p;
            }
        }
        dst->spill_synced = false;
        avoid.Set(dst->p);
        *opr = qir::VOperand::MakePGPR(opr->GetType(), dst->p);
    }
}

// TODO: resurrect allocation for helpers
void QRegAlloc::CallOp(bool use_globals)
{
    for (u8 p = 0; p < N_PREGS; ++p) {
        if (ArchTraits::GPR_CALL_CLOBBER.Test(p))
            Spill(p);
    }

    if (use_globals) {
        for (u8 i = 0; i < n_vregs; ++i) {
            auto *v = &vregs[i];
            if (v->is_global)
                Spill(v);
        }
    }
}

struct QRegAllocVisitor : qir::InstVisitor<QRegAllocVisitor, void> {
    using Base = qir::InstVisitor<QRegAllocVisitor, void>;

public:
    QRegAllocVisitor(QRegAlloc *ra_) : ra(ra_) {}

    void visitInst(UNUSED qir::Inst *ins) { unreachable(""); }

    void visitInstUnop(qir::InstUnop *ins) { ra->AllocOp(ins); }

    void visitInstBinop(qir::InstBinop *ins) { ra->AllocOp(ins); }

    void visitInstSetcc(qir::InstSetcc *ins) { ra->AllocOp(ins); }

    void visitInstBr(UNUSED qir::InstBr *ins)
    {
        // has no voperands
        ra->BlockBoundary();
    }

    void visitInstBrcc(qir::InstBrcc *ins)
    {
        ra->AllocOp(ins);
        ra->BlockBoundary();
    }

    void visitInstGBr(UNUSED qir::InstGBr *ins)
    {
        // has no voperands
        ra->RegionBoundary();
    }

    void visitInstGBrind(qir::InstGBrind *ins)
    {
        ra->AllocOp(ins);
        ra->RegionBoundary();
    }

    void visitInstVMLoad(qir::InstVMLoad *ins) { ra->AllocOp(ins); }

    void visitInstVMStore(qir::InstVMStore *ins) { ra->AllocOp(ins); }

    void visitInstHcall(UNUSED qir::InstHcall *ins) { ra->CallOp(true); }

    void visit_sll(qir::InstBinop *ins) { ra->AllocOp(ins); }

    void visit_srl(qir::InstBinop *ins) { ra->AllocOp(ins); }

    void visit_sra(qir::InstBinop *ins) { ra->AllocOp(ins); }

private:
    QRegAlloc *ra{};
};

void QRegAlloc::Run()
{
    Prologue();

    for (auto &bb : region->GetBlocks()) {
        auto &ilist = bb.ilist;

        for (auto iit = ilist.begin(); iit != ilist.end(); ++iit) {
            qb = qir::Builder(&bb, iit);
            QRegAllocVisitor(this).visit(&*iit);
        }
    }
}

void QRegAllocPass::run(qir::Region *region)
{
    QRegAlloc ra(region);
    ra.Run();
}

}  // namespace dbt::qcg
