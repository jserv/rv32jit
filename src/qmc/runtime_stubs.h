#pragma once

#include <array>

#include "guest/rv32_stubs.h"
#include "util/common.h"

namespace dbt
{
#define COMMON_RUNTIME_STUBS \
    _(escape_link)           \
    _(escape_brind)          \
    _(link_branch_jit)       \
    _(brind)                 \
    _(raise)

#define RUNTIME_STUBS COMMON_RUNTIME_STUBS GUEST_RUNTIME_STUBS

enum class RuntimeStubId {
#define _(name) id_##name,
    RUNTIME_STUBS
#undef _
        Count,
};

struct RuntimeStubTab {
    RuntimeStubTab() : data(g_tab.data) {}

    static RuntimeStubTab const *GetGlobal() { return &g_tab; }

    uptr operator[](RuntimeStubId id) const { return data[to_underlying(id)]; }

    static constexpr uptr offs(RuntimeStubId id)
    {
        return sizeof(uptr) * to_underlying(id);
    }

private:
    using table_t = std::array<uptr, to_underlying(RuntimeStubId::Count)>;

    static const RuntimeStubTab g_tab;
    RuntimeStubTab(table_t data_) : data(data_) {}
    static RuntimeStubTab Create();

    const table_t data;
};

}  // namespace dbt
