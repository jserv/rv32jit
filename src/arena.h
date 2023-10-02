#pragma once

#include <sys/mman.h>

#include "util/common.h"

struct InArena {
    void *operator new(size_t asz UNUSED, void *aptr) { return aptr; }

    void operator delete(void *aptr UNUSED, size_t asz UNUSED)
    {
        unreachable("");
    }
};

struct MemArena {
    MemArena() = default;
    MemArena(size_t size, int prot = PROT_READ | PROT_WRITE)
    {
        Init(size, prot);
    }
    ~MemArena() { Destroy(); }

    void Init(size_t size, int prot = PROT_READ | PROT_WRITE);
    void Destroy();
    void Reset() { used = 0; }

    void *Allocate(size_t alloc_sz, size_t align)
    {
        size_t alloc_start = roundup(used, align);
        if (unlikely(alloc_start + alloc_sz > pool_sz)) {
            assert(0);  // TODO: expand
            return nullptr;
        }
        used = alloc_start + alloc_sz;
        return (void *) (pool + alloc_start);
    }

    template <typename T>
    T *Allocate(size_t num = 1)
    {
        return (T *) Allocate(sizeof(T) * num, alignof(T));
    }

    template <typename T, typename... Args>
    T *New(Args &&...args)
    {
        static_assert(std::is_base_of_v<InArena, T>);
        auto mem = Allocate<T>(1);
        return new (mem) T(std::forward<Args>(args)...);
    }

private:
    u8 *pool{nullptr};
    size_t pool_sz{0};
    size_t used{0};
};
