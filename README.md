# rv32jit

`rv32jit` is a RISC-V instruction set simulator with a JIT assembler as an x86-64 binary translator.

Features
* Fast runtime for executing the RV32 ISA
* Built-in ELF loader
* Implementation of partial Linux system calls

## Build and Verify

Currently, only GNU/Linux is supported for building `rv32jit`. You will need clang version 15+ for the build, which can be downloaded from the [LLVM Page](https://releases.llvm.org/download.html).

Build the emulator:
```shell
$ make
```

You might receive the message "Please run 'make' again."
If you do, simply follow the instruction and run `make` once more.

Download prebuilt RISC-V ELF files and run:
```shell
$ make check
```

## License
`rv32jit` is available under a permissive MIT-style license.
Use of this source code is governed by a MIT license that can be found in the [LICENSE](LICENSE) file.
