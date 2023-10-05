#pragma once

#include "util/common.h"

namespace dbt::qir
{
using Mark = uint16_t;

struct MarkerKeeper;

template <typename N, typename S>
struct Marker {
    Marker(MarkerKeeper *mkeeper, u8 states);

    S Get(N *n)
    {
        Mark m = n->GetMark();
        assert(m < mmax);
        if (m < mmin) {
            return 0;
        }
        return static_cast<S>(m - mmin);
    }

    void Set(N *n, S s)
    {
        auto m = static_cast<Mark>(s);
        assert(n->GetMark() < mmax);
        assert(m + mmin < mmax);
        n->SetMark(m + mmin);
    }

private:
    Marker() = default;
    Mark mmin{0}, mmax{0};
};

struct MarkerKeeper {
private:
    template <typename N, typename S>
    friend struct Marker;

    Mark mmin{0}, mmax{0};
};

template <typename N, typename S>
inline Marker<N, S>::Marker(MarkerKeeper *keeper, u8 states)
{
    mmin = keeper->mmin = keeper->mmax;
    mmax = keeper->mmax = keeper->mmin + states;
}

}  // namespace dbt::qir
