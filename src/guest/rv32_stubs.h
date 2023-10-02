#pragma once

#define GUEST_RUNTIME_STUBS \
    _(rv32_fence)           \
    _(rv32_fencei)          \
    _(rv32_ecall)           \
    _(rv32_ebreak)
