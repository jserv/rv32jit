#pragma once

#include <ostream>

#include "guest/rv32_ops.h"
#include "util/bitfield.h"

namespace dbt::rv32::insn
{
namespace Flags
{
enum Types : u32 {
    None = 0,
    Branch = 1 << 1u,
    MayTrap = 1 << 2u,
    Trap = 1 << 3u,
    HasRd = 1 << 4u,
};
}

#define INSN_FIELD(name) \
    ALWAYS_INLINE constexpr auto name() { return _##name::decode(raw); }
#define INSN_FIELD_SC(name, scale)              \
    ALWAYS_INLINE constexpr auto name()         \
    {                                           \
        return _##name::decode(raw) << (scale); \
    }

struct Base {
    u32 raw;

    INSN_FIELD(opcode)

protected:
    using _opcode = bf_range<u8, 0, 6>;
    using _rd = bf_range<u8, 7, 11>;
    using _rs1 = bf_range<u8, 15, 19>;
    using _rs2 = bf_range<u8, 20, 24>;
    using _funct3 = bf_range<u8, 12, 14>;
    using _funct7 = bf_range<u8, 25, 31>;
    using _funct12 = bf_range<u16, 20, 31>;

    static constexpr Flags::Types gen_flags = Flags::None;
};

struct R : public Base {
    INSN_FIELD(rd)
    INSN_FIELD(rs1)
    INSN_FIELD(rs2)

protected:
    static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct I : public Base {
    INSN_FIELD(rd)
    INSN_FIELD(rs1)
    INSN_FIELD(imm)

protected:
    using _imm = bf_seq<i16, bf_pt<20, 30>, bf_pt<31, 31>>;
    static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct IS : public Base {  // imm shifts
    INSN_FIELD(rd)
    INSN_FIELD(rs1)
    INSN_FIELD(imm)

protected:
    using _imm = bf_range<u8, 20, 24>;
    static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct S : public Base {
    INSN_FIELD(rs1)
    INSN_FIELD(rs2)
    INSN_FIELD(imm)

protected:
    using _imm = bf_seq<i16, bf_pt<7, 11>, bf_pt<25, 30>, bf_pt<31, 31>>;
    static constexpr Flags::Types gen_flags = Flags::None;
};

struct B : public Base {
    INSN_FIELD(rs1)
    INSN_FIELD(rs2)
    INSN_FIELD_SC(imm, 1)

protected:
    using _imm =
        bf_seq<i16, bf_pt<8, 11>, bf_pt<25, 30>, bf_pt<7, 7>, bf_pt<31, 31>>;
    static constexpr Flags::Types gen_flags = Flags::None;
};

struct U : public Base {
    INSN_FIELD(rd)
    INSN_FIELD_SC(imm, 12)

protected:
    using _imm = bf_range<u32, 12, 31>;
    static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct J : public Base {
    INSN_FIELD(rd)
    INSN_FIELD_SC(imm, 1)

protected:
    using _imm =
        bf_seq<i32, bf_pt<21, 30>, bf_pt<20, 20>, bf_pt<12, 19>, bf_pt<31, 31>>;
    static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct A : public Base {
    INSN_FIELD(rd)
    INSN_FIELD(rs1)
    INSN_FIELD(rs2)
    INSN_FIELD(rl)
    INSN_FIELD(aq)
    INSN_FIELD(rlaq)

protected:
    using _rl = bf_range<u8, 25, 25>;
    using _aq = bf_range<u8, 26, 26>;
    using _rlaq = bf_range<u8, 25, 26>;
    static constexpr Flags::Types gen_flags =
        static_cast<Flags::Types>(Flags::HasRd | Flags::MayTrap);
};

#define OP(name, format_, flags_)                                     \
    struct Insn_##name : format_ {                                    \
        using format = format_;                                       \
        static constexpr const char *opcode_str = #name;              \
        static constexpr std::underlying_type_t<Flags::Types> flags = \
            (flags_) | gen_flags;                                     \
    };
RV32_OPCODE_LIST()
#undef OP

enum class Op : u8 {
#define OP(name, format_, flags_) _##name,
    RV32_OPCODE_LIST() OP(last, _, _)
#undef OP
};

}  // namespace dbt::rv32::insn
