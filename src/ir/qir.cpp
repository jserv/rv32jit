#include "ir/qir.h"

namespace dbt::qir
{
template <typename T>
requires requires
{
    {T::n_in};
    {T::n_out};
}
constexpr auto MakeOpInfo(char const *name)
{
    return OpInfo(name, T::n_out, T::n_in);
}

constinit OpInfo op_info[to_underlying(qir::Op::Count)] = {
#define OP(name, base, flags) \
    [to_underlying(qir::Op::_##name)] = MakeOpInfo<qir::base>(#name),
    QIR_OPS_LIST(OP)
#undef OP
};

}  // namespace dbt::qir
