#include <boost/program_options.hpp>
#include <iostream>

#include "env.h"
#include "guest/rv32_cpu.h"
#include "tcache.h"

namespace bpo = boost::program_options;

int main(int argc, char **argv)
{
    int guest_argc = argc - 1;
    char **guest_argv = argv + 1;
    if (guest_argc < 1) {
        std::cout << "empty guest args\n";
        return 1;
    }

    bpo::options_description adesc("options");
    adesc.add_options();
    bpo::variables_map adesc_vm;
    bpo::store(bpo::parse_command_line(1, argv, adesc), adesc_vm);
    bpo::notify(adesc_vm);

    dbt::mmu::Init();
    dbt::tcache::Init();
    dbt::env env{};
    auto elf = &dbt::env::exe_elf_image;
    {
        std::string elf_path = guest_argv[0];
        env.BootElf(elf_path.c_str(), elf);
    }
    env.InitAVectors(elf, guest_argc, guest_argv);

    dbt::CPUState state{};
    dbt::env::InitThread(&state, elf);
    dbt::env::InitSignals(&state);
    int guest_rc = env.Execute(&state);

    dbt::tcache::Destroy();
    dbt::mmu::Destroy();
    return guest_rc;
}
