#pragma once

#define RV32_OPCODE_LIST()         \
    OP(illegal, Base, Flags::Trap) \
    /* RV32I */                    \
    OP(lui, U, 0)                  \
    OP(auipc, U, 0)                \
    OP(jal, J, Flags::Branch)      \
    OP(jalr, I, Flags::Branch)     \
    OP(beq, B, Flags::Branch)      \
    OP(bne, B, Flags::Branch)      \
    OP(blt, B, Flags::Branch)      \
    OP(bge, B, Flags::Branch)      \
    OP(bltu, B, Flags::Branch)     \
    OP(bgeu, B, Flags::Branch)     \
    OP(lb, I, Flags::MayTrap)      \
    OP(lh, I, Flags::MayTrap)      \
    OP(lw, I, Flags::MayTrap)      \
    OP(lbu, I, Flags::MayTrap)     \
    OP(lhu, I, Flags::MayTrap)     \
    OP(sb, S, Flags::MayTrap)      \
    OP(sh, S, Flags::MayTrap)      \
    OP(sw, S, Flags::MayTrap)      \
    OP(addi, I, 0)                 \
    OP(slti, I, 0)                 \
    OP(sltiu, I, 0)                \
    OP(xori, I, 0)                 \
    OP(ori, I, 0)                  \
    OP(andi, I, 0)                 \
    OP(slli, IS, 0)                \
    OP(srai, IS, 0)                \
    OP(srli, IS, 0)                \
    OP(sub, R, 0)                  \
    OP(add, R, 0)                  \
    OP(sll, R, 0)                  \
    OP(slt, R, 0)                  \
    OP(sltu, R, 0)                 \
    OP(xor, R, 0)                  \
    OP(sra, R, 0)                  \
    OP(srl, R, 0)                  \
    OP(or, R, 0)                   \
    OP(and, R, 0)                  \
    OP(fence, Base, 0)             \
    OP(fencei, Base, 0)            \
    OP(ecall, Base, Flags::Trap)   \
    OP(ebreak, Base, Flags::Trap)
