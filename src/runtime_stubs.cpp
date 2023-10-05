#include "runtime_stubs.h"

namespace dbt
{
#define _(name) extern "C" void qcgstub_##name();
RUNTIME_STUBS
#undef _

RuntimeStubTab RuntimeStubTab::Create()
{
    table_t tab;
#define _(name)                                    \
    tab[to_underlying(RuntimeStubId::id_##name)] = \
        reinterpret_cast<uptr>(qcgstub_##name);
    RUNTIME_STUBS
#undef _
    return tab;
}

const RuntimeStubTab RuntimeStubTab::g_tab = RuntimeStubTab::Create();

}  // namespace dbt
