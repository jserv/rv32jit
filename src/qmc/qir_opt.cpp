#include "qmc/qir_opt.h"
#include "qmc/qir_builder.h"

namespace dbt::qir
{
/* TODO: Canonicalize imm ops to simplify constraints matching:
 *  1. Commutative binops
 *  2. setcc & opcode inversion
 */
struct FolderVisitor : qir::InstVisitor<FolderVisitor, bool> {
    using Base = qir::InstVisitor<FolderVisitor, void>;

    FolderVisitor(Block *bb_, Inst *ins_) : qb(bb_, ins_->getIter()) {}

    Inst *Apply()
    {
        auto ins = &*qb.GetIterator();
        if (likely(!visit(ins)))
            return ins;
        auto last_ins = --qb.GetIterator();
        qb.GetBlock()->ilist.erase(ins);
        return &*last_ins;
    }

    bool visitInst(UNUSED Inst *ins) { return false; }

    bool visit_add(InstBinop *ins)
    {
        auto &vs0 = ins->i(0), &vs1 = ins->i(1);
        auto &vd = ins->o(0);
        if (vs0.IsConst()) {
            if (vs1.IsConst()) {
                u32 val = vs0.GetConst() + vs1.GetConst();
                auto opr = VOperand::MakeConst(VType::I32, val);
                qb.Create_mov(vd, opr);
                return true;
            }
            std::swap(vs0, vs1);
        }
        if (vs1.IsConst() && vs1.GetConst() == 0) {
            qb.Create_mov(vd, vs0);
            return true;
        }
        return false;
    }

private:
    Builder qb;
};

Inst *ApplyFolder(Block *bb, Inst *ins)
{
    return FolderVisitor(bb, ins).Apply();
}

}  // namespace dbt::qir
