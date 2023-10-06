#pragma once

#include "guest/rv32_cpu.h"

namespace dbt
{
using uabi_short = i32;
using uabi_ushort = u32;
using uabi_int = i32;
using uabi_uint = u32;
using uabi_long = i32;
using uabi_ulong = u32;
using uabi_llong = i64;
using uabi_ullong = u64;
using uabi_size_t = u32;

struct env {
    struct ElfImage;
    struct Process;

    void BootElf(char const *path, ElfImage *elf);

    void InitArgVectors(ElfImage *elf, int argv_n, char **argv);
    static void InitThread(CPUState *state, ElfImage *elf);
    static void InitSignals(CPUState *state);

    int Execute(CPUState *state);

    void SyscallLinux(CPUState *state);

    static ElfImage exe_elf_image;
    static Process process;

private:
    static void LoadElf(int elf_fd, ElfImage *elf);
};

}  // namespace dbt
