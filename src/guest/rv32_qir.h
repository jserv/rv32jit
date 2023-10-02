#pragma once

#include <array>
#include <map>

#include "guest/rv32_insn.h"
#include "qmc/compile.h"
#include "qmc/qir_builder.h"

namespace dbt::qir::rv32
{
using namespace dbt::rv32;

struct RV32Translator {
#define OP(name, format_, flags_)           \
    void H_##name(void *insn);              \
    void V_##name(rv32::insn::Insn_##name); \
    static constexpr auto _##name = &RV32Translator::H_##name;
    RV32_OPCODE_LIST()
#undef OP

    static void Translate(qir::Region *region,
                          CompilerJob::IpRangesSet *ipranges,
                          uptr vmem);

    static StateInfo const *const state_info;

private:
    static StateInfo const *GetStateInfo();

    explicit RV32Translator(qir::Region *region, uptr vmem);
    void TranslateIPRange(u32 ip, u32 boundary_ip);
    void PreSideeff();
    void TranslateInsn();

    void MakeGBr(u32 ip);

    void TranslateLoad(insn::I i, VType type, VSign sgn);
    void TranslateStore(insn::S i, VType type, VSign sgn);
    void TranslateBrcc(insn::B i, CondCode cc);
    inline void TranslateSetcc(insn::R i, CondCode cc);
    inline void TranslateSetcc(insn::I i, CondCode cc);
    inline void TranslateHelper(insn::Base i, RuntimeStubId stub);

    qir::Builder qb;
    std::map<u32, qir::Block *> ip2bb;

    enum class Control { NEXT, BRANCH, TB_OVF } control{Control::NEXT};
    uptr vmem_base{};
    u32 insn_ip{0};
};

}  // namespace dbt::qir::rv32

namespace dbt::qir
{
using IRTranslator = qir::rv32::RV32Translator;
}
