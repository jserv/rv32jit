#include "qmc/qcg/arch_traits.h"
#include "qmc/qcg/qcg.h"
#include "qmc/qir_builder.h"

namespace dbt::qcg
{
struct QSel {
    QSel(qir::Region *region_, MachineRegionInfo *region_info_)
        : region(region_), region_info(region_info_)
    {
    }

    void Run();

    void SelectOperands(qir::Inst *ins);

    qir::Region *region{};
    qir::Builder qb{nullptr};
    MachineRegionInfo *region_info{};
};

void QSel::SelectOperands(qir::Inst *ins)
{
    auto *op_ct = GetOpInfo(ins->GetOpcode()).ra_ct;
    assert(op_ct);
    auto srcl = ins->inputs();
    auto dstl = ins->outputs();
    auto src_n = srcl.size();
    auto dst_n = dstl.size();
    // satisfy aliases
    for (u8 i = 0; i < src_n; ++i) {
        auto &ct = op_ct[dst_n + i];
        if (!ct.has_alias)
            continue;
        auto *src = &srcl[i];
        auto *dst = &dstl[ct.alias];
        assert(dst->IsVGPR());
        if (src->IsVGPR() && src->GetVGPR() == dst->GetVGPR())
            continue;

        bool live_input = false;
        for (u8 k = 0; k < src_n; ++k) {
            auto *src2 = &srcl[k];
            if (k != i && src2->IsVGPR() && src2->GetVGPR() == dst->GetVGPR()) {
                live_input = true;
                break;
            }
        }
        if (live_input) {
            auto tmp = qir::VOperand::MakeVGPR(dst->GetType(),
                                               qb.CreateVGPR(dst->GetType()));
            qb.Create_mov(tmp, *dst);
            for (u8 k = 0; k < src_n; ++k) {
                auto *src2 = &srcl[k];
                if (k != i && src2->IsVGPR() &&
                    src2->GetVGPR() == dst->GetVGPR()) {
                    *src2 = tmp;
                }
            }
        }
        qb.Create_mov(*dst, *src);
        *src = *dst;
    }
    // lower constants
    for (u8 i = 0; i < src_n; ++i) {
        auto &ct = op_ct[dst_n + i];
        auto *src = &srcl[i];
        if (!src->IsConst())
            continue;

        auto type = src->GetType();
        auto val = src->GetConst();
        if (ct.ci != RACtImm::NO &&
            ArchTraits::match_gp_const(type, val, ct.ci)) {
            continue;
        }
        auto tmp = qir::VOperand::MakeVGPR(type, qb.CreateVGPR(type));
        qb.Create_mov(tmp, *src);
        *src = tmp;
    }
}

struct QSelVisitor : qir::InstVisitor<QSelVisitor, void> {
    using Base = qir::InstVisitor<QSelVisitor, void>;

public:
    QSelVisitor(QSel *sel_) : sel(sel_) {}

    void visitInst(qir::Inst *ins UNUSED) { unreachable(""); }

    void visitInstUnop(qir::InstUnop *ins) { sel->SelectOperands(ins); }

    void visitInstBinop(qir::InstBinop *ins) { sel->SelectOperands(ins); }

    void visitInstSetcc(qir::InstSetcc *ins) { sel->SelectOperands(ins); }

    void visitInstBr(qir::InstBr *ins UNUSED) {}

    void visitInstBrcc(qir::InstBrcc *ins) { sel->SelectOperands(ins); }

    void visitInstGBr(qir::InstGBr *ins UNUSED) {}

    void visitInstGBrind(qir::InstGBrind *ins) { sel->SelectOperands(ins); }

    void visitInstVMLoad(qir::InstVMLoad *ins) { sel->SelectOperands(ins); }

    void visitInstVMStore(qir::InstVMStore *ins) { sel->SelectOperands(ins); }

    void visitInstHcall(qir::InstHcall *ins UNUSED) {}

    void visit_sll(qir::InstBinop *ins) { sel->SelectOperands(ins); }

    void visit_srl(qir::InstBinop *ins) { sel->SelectOperands(ins); }

    void visit_sra(qir::InstBinop *ins) { sel->SelectOperands(ins); }

private:
    QSel *sel{};
};

void QSel::Run()
{
    for (auto &bb : region->GetBlocks()) {
        auto &ilist = bb.ilist;

        for (auto iit = ilist.begin(); iit != ilist.end(); ++iit) {
            qb = qir::Builder(&bb, iit);
            QSelVisitor(this).visit(&*iit);

            if (iit->GetFlags() & qir::Inst::HAS_CALLS)
                region_info->has_calls = true;
        }
    }
}

void QSelPass::run(qir::Region *region, MachineRegionInfo *region_info)
{
    QSel sel(region, region_info);
    sel.Run();
}

}  // namespace dbt::qcg
