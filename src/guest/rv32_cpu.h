#pragma once

#include <array>

#include "guest/rv32_ops.h"
#include "runtime_stubs.h"
#include "tcache.h"
#include "util/common.h"

namespace dbt::rv32
{
enum class TrapCode : u32 {
    NONE = 0,
    UNALIGNED_IP,
    ILLEGAL_INSN,
    EBREAK,
    ECALL,
    TERMINATED,
};

// TODO: separate guest part
struct CPUStateImpl {
    bool IsTrapPending() { return trapno == TrapCode::NONE; }

    using gpr_t = u32;
    static constexpr u8 gpr_num = 32; /* general-purpose registers numbers */

    std::array<gpr_t, gpr_num> gpr{};
    gpr_t ip{};
    TrapCode trapno{};

    tcache::L1BrindCache *l1_brind_cache{&tcache::l1_brind_cache};
    RuntimeStubTab stub_tab{};

    uptr sp_unwindptr{};
};

// qmc config, also used to synchronize JIT debug tracing
static constexpr u16 TB_MAX_INSNS = 64;

}  // namespace dbt::rv32

namespace dbt
{
struct CPUState : rv32::CPUStateImpl {
    static void SetCurrent(CPUState *s) { tls_current = s; }

    static CPUState *Current() { return tls_current; }

private:
    static thread_local CPUState *tls_current;
};
static_assert(std::is_standard_layout_v<CPUState>);
}  // namespace dbt
