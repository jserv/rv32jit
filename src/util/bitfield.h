#pragma once

#include "util/common.h"

template <typename T, size_t l, size_t h>
struct bf_range {
    static_assert(h >= l);
    static constexpr size_t size = h - l + 1;
    static constexpr size_t type_size = sizeof(T) * CHAR_BIT;
    static_assert(type_size >= size);
    static_assert(!std::is_floating_point_v<T> || type_size == size);
    static constexpr size_t container_size = h + 1;

    template <typename C>
    requires std::is_unsigned_v<T>
    static constexpr T decode(C c)
    {
        return decode_bits(static_cast<std::make_unsigned_t<C>>(c));
    }

    template <typename C>
    requires std::is_signed_v<T>
    static constexpr T decode(C c)
    {
        return decode_bits(static_cast<std::make_signed_t<C>>(c));
    }

    template <typename C>
    static constexpr C encode(C c, T t)
    {
        return encode_bits(c, t);
    }

    template <typename C, typename E_>
    requires std::is_enum_v<E_> && std::is_same_v<T, std::underlying_type_t<E_>>
    static constexpr C encode(C c, E_ e)
    {
        return encode_bits(c, to_underlying(e));
    }

    template <typename T_, size_t sz_>
    using next = bf_range<T_, h + 1, h + sz_>;

private:
    static constexpr size_t mask = ((1ull << size) - 1) << l;

    template <typename C>
    static constexpr T decode_bits(C c)
    {
        constexpr size_t u_msb = sizeof(C) * CHAR_BIT - 1;
        static_assert(u_msb >= h);
        return c << (u_msb - h) >> (u_msb - h + l);
    }

    template <typename C>
    static constexpr C encode_bits(C c, T t)
    {
        constexpr size_t u_msb = sizeof(C) * CHAR_BIT - 1;
        static_assert(u_msb >= h);
        constexpr auto type_h = type_size - 1;
        static_assert(u_msb >= type_h);  // C > T
        auto fval = std::make_unsigned_t<C>(t) << (u_msb - h + l) >>
                    (u_msb - h);  // no sgne
        return (c & ~(mask)) | fval;
    }
};

template <typename T, size_t sz>
using bf_first = bf_range<T, 0, sz - 1>;

template <size_t l_, size_t h_>
struct bf_pt {
    static constexpr auto l = l_;
    static constexpr auto h = h_;
};

template <typename T, typename P, typename... Args>
struct bf_seq {
    template <typename C>
    static constexpr T decode(C c)
    {
        return B::decode(c) | (bf_seq<T, Args...>::decode(c) << B::size);
    }

private:
    using B = bf_range<std::make_unsigned_t<T>, P::l, P::h>;
};

template <typename T, typename P>
struct bf_seq<T, P> {
    template <typename C>
    static constexpr T decode(C c)
    {
        return bf_range<T, P::l, P::h>::decode(c);
    }
};
