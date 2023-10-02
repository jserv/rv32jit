#pragma once

#include <vector>

#include "arena.h"

template <typename T>
struct MemArenaSTL {
    using value_type = T;
    using pointer = T *;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    pointer allocate(size_type n) { return arena->Allocate<T>(n); }

    void deallocate(pointer p UNUSED, size_type n UNUSED) {}

    bool operator==(MemArenaSTL const &rhs) { return this->arena == rhs.arena; }

    bool operator!=(MemArenaSTL const &rhs) { return !operator==(rhs); }

    explicit MemArenaSTL(MemArena *arena_) : arena(arena_) {}

    MemArenaSTL(MemArenaSTL const &rhs) : arena(rhs.arena) {}

    template <typename U>
    MemArenaSTL(MemArenaSTL<U> const &rhs) : arena(rhs.arena)
    {
    }

    MemArenaSTL(MemArenaSTL &&rhs) : arena(rhs.arena) { rhs.arena = nullptr; }

    template <typename U>
    MemArenaSTL(MemArenaSTL<U> &&rhs) : arena(rhs.arena)
    {
        rhs.arena = nullptr;
    }

private:
    MemArena *arena{};
};

template <typename T>
using AArena = MemArenaSTL<T>;

template <typename T>
class ArenaVector : public std::vector<T, AArena<T>>
{
public:
    explicit ArenaVector<T>(MemArena *arena)
        : std::vector<T, AArena<T>>(AArena<T>(arena))
    {
    }
};
