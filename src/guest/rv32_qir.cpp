#include "guest/rv32_qir.h"
#include "guest/rv32_cpu.h"
#include "guest/rv32_decode.h"
#include "guest/rv32_ops.h"

namespace dbt::qir::rv32
{
namespace GlobalRegId
{
enum {
    GPR_START = 0,
    GPR_END = 31,
    IP = GPR_END,
    END,
};
}

static inline VOperand vgpr(u8 id, VType type = VType::I32)
{
    assert(id != 0);
    return VOperand::MakeVGPR(type, GlobalRegId::GPR_START + id - 1);
}

static inline VOperand vconst(u32 val, VType type = VType::I32)
{
    return VOperand::MakeConst(type, val);
}

static inline VOperand vtemp(qir::Builder &qb, VType type = VType::I32)
{
    return VOperand::MakeVGPR(type, qb.CreateVGPR(type));
}

static inline VOperand gprop(u8 id, VType type = VType::I32)
{
    if (!id)
        return vconst(0, type);
    return vgpr(id, type);
}

StateInfo const *RV32Translator::GetStateInfo()
{
    static std::array<StateReg, GlobalRegId::END> state_regs{};
    static StateInfo state_info{state_regs.data(), state_regs.size()};

    for (u8 i = 1; i < 32; ++i) {
        u16 offs = offsetof(CPUState, gpr) + 4 * i;
        int vreg_no = GlobalRegId::GPR_START + i - 1;
        state_regs[vreg_no] = StateReg{offs, VType::I32};
    }
    state_regs[GlobalRegId::IP] = StateReg{offsetof(CPUState, ip), VType::I32};

    return &state_info;
}
StateInfo const *const RV32Translator::state_info = GetStateInfo();

RV32Translator::RV32Translator(UNUSED qir::Region *region_, uptr vmem)
    : qb(), vmem_base(vmem)
{
}

void RV32Translator::Translate(qir::Region *region,
                               CompilerJob::IpRangesSet *ipranges,
                               uptr vmem)
{
    RV32Translator t(region, vmem);

    for (auto const &range : *ipranges)
        t.ip2bb.insert({range.first, region->CreateBlock()});

    for (auto const &range : *ipranges)
        t.TranslateIPRange(range.first, range.second);
}

void RV32Translator::TranslateIPRange(u32 ip, u32 boundary_ip)
{
    insn_ip = ip;
    assert(boundary_ip != 0);

    qb = qir::Builder(ip2bb.find(ip)->second);

    u32 num_insns = 0;
    control = Control::NEXT;
    while (true) {
        TranslateInsn();
        num_insns++;
        if (control != Control::NEXT)
            break;
        if (num_insns == TB_MAX_INSNS || insn_ip >= boundary_ip) {
            control = Control::TB_OVF;
            MakeGBr(insn_ip);
            break;
        }
    }
}

// TODO: move to late qir pass?
void RV32Translator::PreSideeff()
{
    auto offs = state_info->GetStateReg(GlobalRegId::IP)->state_offs;
    auto ip_spill = qir::VOperand::MakeSlot(true, qir::VType::I32, offs);
    qb.Create_mov(ip_spill, vconst(insn_ip, qir::VType::I32));
}

void RV32Translator::TranslateInsn()
{
    auto *insn_ptr = (u32 *) (vmem_base + insn_ip);

    using decoder = insn::Decoder<RV32Translator>;
    (this->*decoder::Decode(insn_ptr))(insn_ptr);
}

void RV32Translator::MakeGBr(u32 ip)
{
    auto it = ip2bb.find(ip);
    if (it != ip2bb.end()) {
        qb.Create_br();
        qb.GetBlock()->AddSucc(it->second);
    } else {
        qb.Create_gbr(vconst(ip));
    }
}

void RV32Translator::TranslateBrcc(rv32::insn::B i, CondCode cc)
{
#if 1
    auto make_target = [&](u32 ip) {
        auto it = ip2bb.find(ip);
        if (it != ip2bb.end()) {
            return it->second;
        } else {
            qb = Builder(qb.CreateBlock());
            qb.Create_gbr(vconst(ip));
            return qb.GetBlock();
        }
    };

    auto bb_src = qb.GetBlock();
    auto bb_f = make_target(insn_ip + 4);
    auto bb_t = make_target(insn_ip + i.imm());
    qb = Builder(bb_src);

    qb.Create_brcc(cc, gprop(i.rs1()), gprop(i.rs2()));
    qb.GetBlock()->AddSucc(bb_t);
    qb.GetBlock()->AddSucc(bb_f);
#else  // TODO: qir cleanup pass: remove empty bb
    auto bb_f = qb.CreateBlock();
    auto bb_t = qb.CreateBlock();

    qb.GetBlock()->AddSucc(bb_t);
    qb.GetBlock()->AddSucc(bb_f);
    qb.Create_brcc(cc, gprop(i.rs1()), gprop(i.rs2()));

    qb = Builder(bb_t);
    MakeGBr(insn_ip + i.imm());

    qb = Builder(bb_f);
    MakeGBr(insn_ip + 4);
#endif
}

inline void RV32Translator::TranslateSetcc(rv32::insn::R i, CondCode cc)
{
    if (i.rd())
        qb.Create_setcc(cc, vgpr(i.rd()), gprop(i.rs1()), gprop(i.rs2()));
}

inline void RV32Translator::TranslateSetcc(rv32::insn::I i, CondCode cc)
{
    if (i.rd())
        qb.Create_setcc(cc, vgpr(i.rd()), gprop(i.rs1()), vconst(i.imm()));
}

void RV32Translator::TranslateLoad(insn::I i, VType type, VSign sgn)
{
    VOperand addr = gprop(i.rs1());

    if (i.imm()) {
        auto tmp = vtemp(qb);
        qb.Create_add(tmp, addr, vconst(i.imm()));  // constfold
        addr = tmp;
    }
    if (i.rd()) {
        qb.Create_vmload(type, sgn, vgpr(i.rd()), addr);
    } else {
        qb.Create_vmload(type, sgn, addr, addr);
    }
}

void RV32Translator::TranslateStore(insn::S i, VType type, VSign sgn)
{
    VOperand addr = gprop(i.rs1());

    if (i.imm()) {
        auto tmp = vtemp(qb);
        qb.Create_add(tmp, addr, vconst(i.imm()));
        addr = tmp;
    }
    qb.Create_vmstore(type, sgn, addr, gprop(i.rs2(), type));
}

inline void RV32Translator::TranslateHelper(insn::Base i, RuntimeStubId stub)
{
    qb.Create_hcall(stub, vconst(i.raw));
}

#define TRANSLATOR(name)                                  \
    void RV32Translator::H_##name(void *insn)             \
    {                                                     \
        insn::Insn_##name i{*(u32 *) insn};               \
        static constexpr auto flags = decltype(i)::flags; \
        if constexpr (flags & insn::Flags::Trap ||        \
                      flags & insn::Flags::MayTrap) {     \
            PreSideeff();                                 \
        }                                                 \
        V_##name(i);                                      \
        if constexpr (flags & insn::Flags::Branch ||      \
                      flags & insn::Flags::Trap) {        \
            control = RV32Translator::Control::BRANCH;    \
        }                                                 \
        insn_ip += 4;                                     \
    }                                                     \
    ALWAYS_INLINE void RV32Translator::V_##name(UNUSED insn::Insn_##name i)

#define TRANSLATOR_Unimpl(name) \
    TRANSLATOR(name) { dbt::Panic("Unimplemented instruction " #name); }

#define TRANSLATOR_ArithmRI(name, op)                                      \
    TRANSLATOR(name)                                                       \
    {                                                                      \
        if (i.rd())                                                        \
            qb.Create_##op(vgpr(i.rd()), gprop(i.rs1()), vconst(i.imm())); \
    }

#define TRANSLATOR_ArithmRR(name, op)                                     \
    TRANSLATOR(name)                                                      \
    {                                                                     \
        if (i.rd())                                                       \
            qb.Create_##op(vgpr(i.rd()), gprop(i.rs1()), gprop(i.rs2())); \
    }

#define TRANSLATOR_Brcc(name, cc) \
    TRANSLATOR(name) { TranslateBrcc(i, CondCode::cc); }

#define TRANSLATOR_Setcc(name, cc) \
    TRANSLATOR(name) { TranslateSetcc(i, CondCode::cc); }

#define TRANSLATOR_Load(name, type, sgn) \
    TRANSLATOR(name) { TranslateLoad(i, VType::type, VSign::sgn); }

#define TRANSLATOR_Store(name, type, sgn) \
    TRANSLATOR(name) { TranslateStore(i, VType::type, VSign::sgn); }

#define TRANSLATOR_Helper(name) \
    TRANSLATOR(name) { TranslateHelper(i, RuntimeStubId::id_rv32_##name); }

TRANSLATOR_Unimpl(illegal);
TRANSLATOR(lui)
{
    if (i.rd())
        qb.Create_mov(vgpr(i.rd()), vconst(i.imm()));
}
TRANSLATOR(auipc)
{
    if (i.rd())
        qb.Create_mov(vgpr(i.rd()), vconst(i.imm() + insn_ip));
}
TRANSLATOR(jal)
{
    // TODO: check alignment
    if (i.rd())
        qb.Create_mov(vgpr(i.rd()), vconst(insn_ip + 4));

    MakeGBr(insn_ip + i.imm());
}
TRANSLATOR(jalr)
{
    // TODO: check alignment
    auto tgt = vtemp(qb);

    // constfold
    qb.Create_add(tgt, gprop(i.rs1()), vconst(i.imm()));
    qb.Create_and(tgt, tgt, vconst(~(u32) 1));

    if (i.rd())
        qb.Create_mov(vgpr(i.rd()), vconst(insn_ip + 4));

    qb.Create_gbrind(tgt);
}
TRANSLATOR_Brcc(beq, EQ);
TRANSLATOR_Brcc(bne, NE);
TRANSLATOR_Brcc(blt, LT);
TRANSLATOR_Brcc(bge, GE);
TRANSLATOR_Brcc(bltu, LTU);
TRANSLATOR_Brcc(bgeu, GEU);
TRANSLATOR_Load(lb, I8, S);
TRANSLATOR_Load(lh, I16, S);
TRANSLATOR_Load(lw, I32, S);
TRANSLATOR_Load(lbu, I8, U);
TRANSLATOR_Load(lhu, I16, U);
TRANSLATOR_Store(sb, I8, U);
TRANSLATOR_Store(sh, I16, U);
TRANSLATOR_Store(sw, I32, U);
TRANSLATOR_ArithmRI(addi, add);
TRANSLATOR_Setcc(slti, LT);
TRANSLATOR_Setcc(sltiu, LTU);
TRANSLATOR_ArithmRI(xori, xor);
TRANSLATOR_ArithmRI(ori, or);
TRANSLATOR_ArithmRI(andi, and);
TRANSLATOR_ArithmRI(slli, sll);
TRANSLATOR_ArithmRI(srai, sra);
TRANSLATOR_ArithmRI(srli, srl);
TRANSLATOR_ArithmRR(sub, sub);
TRANSLATOR_ArithmRR(add, add);
TRANSLATOR_ArithmRR(sll, sll);
TRANSLATOR_Setcc(slt, LT);
TRANSLATOR_Setcc(sltu, LTU);
TRANSLATOR_ArithmRR(xor, xor);
TRANSLATOR_ArithmRR(sra, sra);
TRANSLATOR_ArithmRR(srl, srl);
TRANSLATOR_ArithmRR(or, or);
TRANSLATOR_ArithmRR(and, and);
TRANSLATOR_Helper(fence);
TRANSLATOR_Helper(fencei);
TRANSLATOR_Helper(ecall);
TRANSLATOR_Helper(ebreak);

}  // namespace dbt::qir::rv32
