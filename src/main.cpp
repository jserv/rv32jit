#include <iostream>

#include "env.h"
#include "guest/rv32_cpu.h"
#include "tcache.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cout << "A RISC-V ELF file is required to run this simulator.\n";
        return 1;
    }

    dbt::mmu::Init();
    dbt::tcache::Init();
    dbt::env env{};
    auto elf = &dbt::env::exe_elf_image;
    env.BootElf(argv[1], elf);
    env.InitArgVectors(elf, argc - 1, argv + 1);

    dbt::CPUState state{};
    dbt::env::InitThread(&state, elf);
    dbt::env::InitSignals(&state);
    int guest_rc = env.Execute(&state);

    dbt::tcache::Destroy();
    dbt::mmu::Destroy();
    return guest_rc;
}
