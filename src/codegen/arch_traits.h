#pragma once

#include "codegen/asmjit_deps.h"
#include "ir/qir.h"

namespace dbt::qcg
{
struct RegMask {
    constexpr RegMask(u32 data_) : data(data_) {}

    constexpr bool Test(qir::RegN r) const { return data & (1u << r); }
    constexpr RegMask &Set(qir::RegN r)
    {
        data |= (1u << r);
        return *this;
    }
    constexpr u8 count() const { return std::popcount(data); }
    constexpr RegMask operator&(RegMask rh) const
    {
        return RegMask{data & rh.data};
    }
    constexpr RegMask operator|(RegMask rh) const
    {
        return RegMask{data | rh.data};
    }
    constexpr RegMask operator~() const { return RegMask{~data}; }

private:
    u32 data;
};

enum class RACtImm : u8 {
    NO = 0 << 0,
    ANY = 1 << 0,
    // amd64
    S32 = 1 << 1,
    U32 = 1 << 2,
};
DEFINE_ENUM_CLASS_FLAGOPS(RACtImm)

struct RAOpCt {
    constexpr void SetAlias(u8 alias_)
    {
        has_alias = true;
        alias = alias_;
    }

    RegMask cr{0};
    RACtImm ci{};
    bool has_alias{};
    u8 alias{};
};

namespace ArchTraits
{
static constexpr u8 GPR_NUM = 16;

#define _(name, id) \
    UNUSED static constexpr auto name = asmjit::x86::Gp::kId##id;
// all gpr
_(RAX, Ax);
_(RCX, Cx);
_(RDX, Dx);
_(RBX, Bx);
_(RSP, Sp);
_(RBP, Bp);
_(RSI, Si);
_(RDI, Di);
_(R8, R8);
_(R9, R9);
_(R10, R10);
_(R11, R11);
_(R12, R12);
_(R13, R13);
_(R14, R14);
_(R15, R15);
#undef _

#define QMC_FIXED_REGS           \
    _(STATE, R13)   /* ghccc0 */ \
    _(MEMBASE, RBP) /* ghccc1 */ \
    _(SP, RSP)

#define _(name, reg) UNUSED static constexpr auto name = reg;
QMC_FIXED_REGS
#undef _

#define _(name, reg) .Set(name)
static constexpr RegMask GPR_FIXED = RegMask(0) QMC_FIXED_REGS;
#undef _

static constexpr RegMask GPR_CALL_CLOBBER = RegMask(0)
                                                .Set(RAX)
                                                .Set(RDI)
                                                .Set(RSI)
                                                .Set(RDX)
                                                .Set(RCX)
                                                .Set(R8)
                                                .Set(R9)
                                                .Set(R10)
                                                .Set(R11);

static constexpr RegMask GPR_ALL(((u32) 1 << GPR_NUM) - 1);
static constexpr RegMask GPR_POOL = GPR_ALL & ~GPR_FIXED;
static constexpr RegMask GPR_CALL_SAVED = GPR_ALL & ~GPR_CALL_CLOBBER;

static constexpr u16 spillframe_size = 1024;  // TODO: reuse temps

bool match_gp_const(qir::VType type, i64 val, RACtImm ct);

void init();
}  // namespace ArchTraits

}  // namespace dbt::qcg
