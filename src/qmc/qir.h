#pragma once

#include <array>
#include <bit>
#include <vector>

#include "arena_objects.h"
#include "qmc/ilist.h"
#include "qmc/qir_ops.h"
#include "qmc/runtime_stubs.h"
#include "util/bitfield.h"

namespace dbt::qcg
{
struct RAOpCt;
};

namespace dbt::qir
{
template <typename D, typename B>
requires std::is_base_of_v<B, D> ALWAYS_INLINE D *as(B *b)
{
    if (!D::classof(b))
        return nullptr;
    return static_cast<D *>(b);
}

template <typename D, typename B>
ALWAYS_INLINE D *cast(B *b)
{
    auto res = as<D>(b);
    assert(res);
    return res;
}

enum class Op : u8 {
#define OP(name, base, flags) _##name,
#define CLASS(cls, beg, end) cls##_begin = _##beg, cls##_end = _##end,
    QIR_OPS_LIST(OP) QIR_CLASS_LIST(CLASS)
#undef OP
#undef CLASS
        Count,
};

struct OpInfo {
    OpInfo() = delete;
    constexpr OpInfo(char const *name_, u8 n_out_, u8 n_in_)
        : name(name_), n_out(n_out_), n_in(n_in_)
    {
    }

    char const *name;
    const u8 n_out;
    const u8 n_in;

    qcg::RAOpCt const *ra_ct{};
    u8 const *ra_order{};
};

extern OpInfo op_info[to_underlying(qir::Op::Count)];

inline OpInfo const &GetOpInfo(qir::Op op)
{
    return op_info[to_underlying(op)];
}

enum class VType : u8 {
    UNDEF,
    I8,
    I16,
    I32,
    Count,
};

inline u8 VTypeToSize(VType type)
{
    switch (type) {
    case VType::I8:
        return 1;
    case VType::I16:
        return 2;
    case VType::I32:
        return 4;
    default:
        unreachable("");
    }
}

enum class VSign : u8 {
    U = 0,
    S = 1,
};

using RegN = u16;
static constexpr auto RegNBad = static_cast<RegN>(-1);

struct VOperand {
private:
    enum class Kind : u8 {
        CONST = 0,
        GPR,
        SLOT,
        // FPR
        BAD,
        Count,
    };

    VOperand(uptr value_) : value(value_) {}

public:
    explicit VOperand() : value(f_kind::encode(uptr(0), Kind::BAD)) {}

    DEFAULT_COPY(VOperand)
    DEFAULT_MOVE(VOperand)

    static VOperand MakeVGPR(VType type, RegN reg)
    {
        uptr value = 0;
        value = f_kind::encode(value, Kind::GPR);
        value = f_type::encode(value, type);
        value = f_is_virtual::encode(value, true);
        value = f_reg::encode(value, reg);
        return VOperand(value);
    }

    static VOperand MakePGPR(VType type, RegN reg)
    {
        uptr value = 0;
        value = f_kind::encode(value, Kind::GPR);
        value = f_type::encode(value, type);
        value = f_reg::encode(value, reg);
        return VOperand(value);
    }

    static VOperand MakeConst(VType type, u32 cval)
    {
        uptr value = 0;
        value = f_kind::encode(value, Kind::CONST);
        value = f_type::encode(value, type);
        value = f_const::encode(value, cval);
        return VOperand(value);
    }

    static VOperand MakeSlot(bool is_glob, VType type, u16 offs)
    {
        uptr value = 0;
        value = f_kind::encode(value, Kind::SLOT);
        value = f_type::encode(value, type);
        value = f_slot_offs::encode(value, offs);
        value = f_slot_is_global::encode(value, is_glob);
        return VOperand(value);
    }

    VType GetType() const { return static_cast<VType>(f_type::decode(value)); }

    bool IsConst() const { return GetKind() == Kind::CONST; }

    // preg or vreg
    bool IsGPR() const { return GetKind() == Kind::GPR; }

    bool IsSlot() const { return GetKind() == Kind::SLOT; }

    bool IsV() const
    {
        assert(IsGPR());
        return FlagV();
    }

    bool IsPGPR() const { return IsGPR() && !FlagV(); }

    bool IsVGPR() const { return IsGPR() && FlagV(); }

    bool IsGSlot() const { return IsSlot() && f_slot_is_global::decode(value); }

    bool IsLSlot() const
    {
        return IsSlot() && !f_slot_is_global::decode(value);
    }

    u32 GetConst() const
    {
        assert(IsConst());
        return f_const::decode(value);
    }

    RegN GetPGPR() const
    {
        assert(IsPGPR());
        return f_reg::decode(value);
    }

    RegN GetVGPR() const
    {
        assert(IsVGPR());
        return f_reg::decode(value);
    }

    u16 GetSlotOffs() const
    {
        assert(IsSlot());
        return f_slot_offs::decode(value);
    }

private:
    Kind GetKind() const { return static_cast<Kind>(f_kind::decode(value)); }

    bool FlagV() const { return f_is_virtual::decode(value); }

    uptr value{0};

    using f_kind =
        bf_first<std::underlying_type_t<Kind>, enum_bits(Kind::Count)>;
    using f_type =
        f_kind::next<std::underlying_type_t<VType>, enum_bits(VType::Count)>;
    using f_is_virtual = f_type::next<bool, 1>;
    using last_ = f_is_virtual;

    static constexpr auto data_bits = bit_size<uptr> - last_::container_size;
    using f_reg = last_::next<RegN, bit_size<RegN>>;
    using f_const = last_::next<u32, 32>;  // TODO: cpool
    using f_slot_offs = last_::next<u16, 16>;
    using f_slot_is_global = f_slot_offs::next<bool, 1>;
};

struct VOperandSpan {
    VOperandSpan(VOperand *head_, u8 size_) : head(head_), len(size_) {}

    VOperand &operator[](u8 idx) const
    {
        assert(idx < len);
        return head[-idx];
    }

    u8 size() const { return len; }

private:
    VOperand *head;
    u8 len;
};

template <typename Derived>
struct InstOperandAccessMixin {
    VOperand &o(u8 idx)
    {
        assert(idx < d()->OutputCount());
        return d()->GetOperand(idx);
    }

    VOperand &i(u8 idx)
    {
        assert(idx < d()->InputCount());
        return d()->GetOperand(idx + d()->OutputCount());
    }

    VOperandSpan outputs()
    {
        return VOperandSpan(&d()->GetOperand(0), d()->OutputCount());
    }

    VOperandSpan inputs()
    {
        return VOperandSpan(&d()->GetOperand(d()->OutputCount()),
                            d()->InputCount());
    }

private:
    Derived *d() { return static_cast<Derived *>(this); }
};

struct alignas(alignof(VOperand)) Inst : IListNode<Inst>,
                                         InstOperandAccessMixin<Inst>,
                                         InArena {
    friend struct InstOperandAccessMixin<Inst>;

    enum Flags : u8 {  // TODO: enum class
        SIDEEFF = 1 << 0,
        REXIT = 1 << 1,
        HAS_CALLS = 1 << 2,
    };

    Op GetOpcode() const { return opcode; }

    u32 GetId() const { return id; }

    Flags GetFlags() const { return flags; }

    void SetFlags(Flags flags_) { flags = flags_; }

    auto OutputCount() const { return GetOpInfo(GetOpcode()).n_out; }

    auto InputCount() const { return GetOpInfo(GetOpcode()).n_in; }

protected:
    Inst(Op opcode_) : opcode(opcode_) {}

    VOperand &GetOperand(u8 idx)
    {
        return reinterpret_cast<VOperand *>(this)[-1 - idx];
    }

private:
    template <typename T, typename... Args>
    requires std::is_base_of_v<Inst, T>
    static T *New(MemArena *arena, u32 id, Flags flags, Args &&...args)
    {
        size_t n_ops = T::OutputCount() + T::InputCount();
        size_t ops_size = sizeof(VOperand) * n_ops;
        auto *mem = arena->Allocate(ops_size + sizeof(T), alignof(VOperand));
        auto res = new ((u8 *) mem + ops_size) T(std::forward<Args>(args)...);
        res->id = id;
        res->flags = flags;
        return res;
    }

    NO_COPY(Inst)
    NO_MOVE(Inst)

    friend struct Region;

    u32 id{(u32) -1};
    Op opcode;
    Flags flags{};
};

inline Inst::Flags GetOpFlags(Op op)
{
    using Flags = Inst::Flags;
    switch (op) {
#define OP(name, base, flags) \
    case Op::_##name:         \
        return Inst::Flags(flags);
        QIR_OPS_LIST(OP)
#undef OP
    default:
        unreachable("");
    };
}

struct InstNoOperands : Inst {
protected:
    InstNoOperands(Op opcode_) : Inst(opcode_) {}

public:
    constexpr static auto OutputCount() { return 0; }

    constexpr static auto InputCount() { return 0; }

    static constexpr u8 n_out = 0;
    static constexpr u8 n_in = 0;
};

template <size_t N_OUT, size_t N_IN>
struct InstWithOperands
    : Inst,
      InstOperandAccessMixin<InstWithOperands<N_OUT, N_IN>> {
    friend struct InstOperandAccessMixin<InstWithOperands<N_OUT, N_IN>>;

protected:
    InstWithOperands(Op opcode_,
                     std::array<VOperand, N_OUT> &&o_,
                     std::array<VOperand, N_IN> &&i_)
        : Inst(opcode_)
    {
        // TODO: iterators
        for (u8 k = 0; k < N_OUT; ++k)
            GetOperand(k) = o_[k];
        for (u8 k = 0; k < N_IN; ++k)
            GetOperand(N_OUT + k) = i_[k];
    }

public:
    constexpr static auto OutputCount() { return N_OUT; }

    constexpr static auto InputCount() { return N_IN; }

    using InstOperandAccessMixin::i;
    using InstOperandAccessMixin::inputs;
    using InstOperandAccessMixin::o;
    using InstOperandAccessMixin::outputs;

    static constexpr u8 n_out = N_OUT;
    static constexpr u8 n_in = N_IN;
};

/* Common classes */

struct InstUnop : InstWithOperands<1, 1> {
    InstUnop(Op opcode_, VOperand d, VOperand s)
        : InstWithOperands(opcode_, {d}, {s})
    {
        assert(HasOpcode(opcode_));
    }

    static bool classof(Inst *op) { return HasOpcode(op->GetOpcode()); }

    static bool HasOpcode(Op opcode)
    {
        return opcode >= Op::InstUnop_begin && opcode <= Op::InstUnop_end;
    }
};

struct InstBinop : InstWithOperands<1, 2> {
    InstBinop(Op opcode_, VOperand d, VOperand sl, VOperand sr)
        : InstWithOperands(opcode_, {d}, {sl, sr})
    {
        assert(HasOpcode(opcode_));
    }

    static bool classof(Inst *op) { return HasOpcode(op->GetOpcode()); }

    static bool HasOpcode(Op opcode)
    {
        return opcode >= Op::InstBinop_begin && opcode <= Op::InstBinop_end;
    }
};

/* Custom classes */

struct Block;

struct InstBr : InstNoOperands {
    InstBr() : InstNoOperands(Op::_br) {}
};

// TODO: compact and fast encoding
enum class CondCode : u8 {
    EQ,
    NE,
    LE,
    LT,
    GE,
    GT,
    LEU,
    LTU,
    GEU,
    GTU,
    Count,
};

inline CondCode InverseCC(CondCode cc)
{
    switch (cc) {
    case CondCode::EQ:
        return CondCode::NE;
    case CondCode::NE:
        return CondCode::EQ;
    case CondCode::LE:
        return CondCode::GT;
    case CondCode::LT:
        return CondCode::GE;
    case CondCode::GE:
        return CondCode::LT;
    case CondCode::GT:
        return CondCode::LE;
    case CondCode::LEU:
        return CondCode::GTU;
    case CondCode::LTU:
        return CondCode::GEU;
    case CondCode::GEU:
        return CondCode::LTU;
    case CondCode::GTU:
        return CondCode::LEU;
    default:
        unreachable("");
    }
}

inline CondCode SwapCC(CondCode cc)
{
    switch (cc) {
    case CondCode::EQ:
        return CondCode::EQ;
    case CondCode::NE:
        return CondCode::NE;
    case CondCode::LE:
        return CondCode::GE;
    case CondCode::LT:
        return CondCode::GT;
    case CondCode::GE:
        return CondCode::LE;
    case CondCode::GT:
        return CondCode::LT;
    case CondCode::LEU:
        return CondCode::GEU;
    case CondCode::LTU:
        return CondCode::GTU;
    case CondCode::GEU:
        return CondCode::LEU;
    case CondCode::GTU:
        return CondCode::LTU;
    default:
        unreachable("");
    }
}

struct InstBrcc : InstWithOperands<0, 2> {
    InstBrcc(CondCode cc_, VOperand s1, VOperand s2)
        : InstWithOperands(Op::_brcc, {}, {s1, s2}), cc(cc_)
    {
    }

    CondCode cc;
};

struct InstGBr : InstNoOperands {
    InstGBr(VOperand tpc_) : InstNoOperands(Op::_gbr), tpc(tpc_)
    {
        assert(tpc_.IsConst());
    }

    VOperand tpc;
};

struct InstGBrind : InstWithOperands<0, 1> {
    InstGBrind(VOperand tpc_) : InstWithOperands(Op::_gbrind, {}, {tpc_}) {}
};

struct InstHcall : InstWithOperands<0, 1> {
    // TODO: variable number of operands
    InstHcall(RuntimeStubId stub_, VOperand arg_)
        : InstWithOperands(Op::_hcall, {}, {arg_}), stub(stub_)
    {
    }

    RuntimeStubId stub;
};

struct InstVMLoad : InstWithOperands<1, 1> {
    InstVMLoad(VType sz_, VSign sgn_, VOperand d, VOperand ptr)
        : InstWithOperands(Op::_vmload, {d}, {ptr}), sz(sz_), sgn(sgn_)
    {
    }

    VType sz;
    VSign sgn;
};

struct InstVMStore : InstWithOperands<0, 2> {
    InstVMStore(VType sz_, VSign sgn_, VOperand ptr, VOperand val)
        : InstWithOperands(Op::_vmstore, {}, {ptr, val}), sz(sz_), sgn(sgn_)
    {
    }

    VType sz;
    VSign sgn;
};

struct InstSetcc : InstWithOperands<1, 2> {
    InstSetcc(CondCode cc_, VOperand d, VOperand sl, VOperand sr)
        : InstWithOperands(Op::_setcc, {d}, {sl, sr}), cc(cc_)
    {
    }

    CondCode cc;
};

struct Block;
struct Region;
inline MemArena *ArenaOf(Region *rn);

struct Block : IListNode<Block>, InArena {
    Block(Region *rn_, u32 id_)
        : rn(rn_), succs(ArenaOf(rn)), preds(ArenaOf(rn)), id(id_)
    {
    }

    IList<Inst> ilist;

    Region *GetRegion() const { return rn; }

    u32 GetId() const { return id; }

    void AddSucc(Block *succ)
    {
        succs.push_back(succ);
        succ->preds.push_back(this);
    }

    auto &GetSuccs() { return succs; }
    auto &GetPreds() { return preds; }

private:
    Region *rn;
    ArenaVector<Block *> succs, preds;
    u32 id{(u32) -1};
};

struct StateReg {
    u16 state_offs;
    VType type;
};

struct StateInfo {
    StateReg const *GetStateReg(RegN idx) const
    {
        if (idx < n_regs) {
            return &regs[idx];
        }
        return nullptr;
    }

    StateReg *regs{};
    RegN n_regs{};
};

struct VRegsInfo {
    VRegsInfo(StateInfo const *glob_info_) : glob_info(glob_info_) {}

    auto NumGlobals() const { return glob_info->n_regs; }

    auto NumLocals() const { return loc_info.size(); }

    auto NumAll() const { return glob_info->n_regs + loc_info.size(); }

    bool IsGlobal(RegN idx) const { return idx < glob_info->n_regs; }

    bool IsLocal(RegN idx) const { return !IsGlobal(idx); }

    StateReg const *GetGlobalInfo(RegN idx) const
    {
        assert(IsGlobal(idx));
        return &glob_info->regs[idx];
    }

    VType GetLocalType(RegN idx) const
    {
        assert(IsLocal(idx));
        return loc_info[idx - glob_info->n_regs];
    }

    RegN AddLocal(VType type)
    {
        auto idx = loc_info.size() + glob_info->n_regs;
        loc_info.push_back(type);
        return idx;
    }

private:
    StateInfo const *glob_info{};
    std::vector<VType> loc_info{};
};

struct Region : InArena {
    explicit Region(MemArena *arena_, StateInfo const *state_info_)
        : arena(arena_), vregs_info(state_info_)
    {
    }

    auto &GetBlocks() { return blist; }

    Block *CreateBlock()
    {
        auto bb = arena->New<Block>(this, bb_id_counter++);
        blist.push_back(bb);
        return bb;
    }

    template <typename T, typename... Args>
    requires std::is_base_of_v<Inst, T> T *Create(Inst::Flags flags,
                                                  Args &&...args)
    {
        return Inst::New<T>(arena, inst_id_counter++, flags,
                            std::forward<Args>(args)...);
    }

    u32 GetNumBlocks() const { return bb_id_counter; }

    MemArena *GetArena() { return arena; }

    VRegsInfo *GetVRegsInfo() { return &vregs_info; }

private:
    MemArena *arena;
    IList<Block> blist;

    VRegsInfo vregs_info;

    u32 inst_id_counter{0};
    u32 bb_id_counter{0};
};

MemArena *ArenaOf(Region *rn)
{
    return rn->GetArena();
}

template <typename Derived, typename RT>
struct InstVisitor {
#define VIS_CLASS(cls) \
    return static_cast<Derived *>(this)->visit##cls(static_cast<cls *>(ins))

#define OP(name, cls, flags) \
    RT visit_##name(cls *ins) { VIS_CLASS(cls); }
    QIR_OPS_LIST(OP)
#undef OP

#define CLASS(cls, beg, end) \
    RT visit##cls(cls *ins) { VIS_CLASS(Inst); }
    QIR_CLASS_LIST(CLASS)
#undef CLASS

    void visitInst(Inst *ins) {}

    RT visit(Inst *ins)
    {
        switch (ins->GetOpcode()) {
#define OP(name, cls, flags)                               \
    case Op::_##name:                                      \
        return static_cast<Derived *>(this)->visit_##name( \
            static_cast<cls *>(ins));
            QIR_OPS_LIST(OP)
#undef OP
        default:
            unreachable("");
        };
    }
};

}  // namespace dbt::qir
