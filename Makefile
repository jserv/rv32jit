CXX := clang++-15
CXXFLAGS := -O2 -flto -Wall -Wextra -Wno-c99-designator
CXXFLAGS += -I src
CXXFLAGS += -std=gnu++20 -fno-rtti -fexceptions

# C++ Boost
CXXFLAGS += -DBOOST_ALL_NO_LIB -DBOOST_PROGRAM_OPTIONS_DYN_LINK
LDFLAGS += -lboost_program_options

LDFLAGS += -ldl -lrt

include mk/common.mk

OUT ?= build

# AsmJit
ASMJIT_DIR = external/asmjit/src
CXXFLAGS += -I $(ASMJIT_DIR)
CXXFLAGS += -DASMJIT_STATIC -DASMJIT_NO_AARCH32 -DASMJIT_NO_AARCH64 -DASMJIT_NO_FOREIGN -DASMJIT_NO_COMPILER
ASMJIT_SRCS := \
        $(wildcard $(ASMJIT_DIR)/asmjit/core/*.cpp) \
        $(wildcard $(ASMJIT_DIR)/asmjit/x86/*.cpp)

OBJS := $(patsubst $(ASMJIT_DIR)/%.cpp,%.o,$(ASMJIT_SRCS))

OBJS += \
	util/common.o \
	\
	arena.o \
	mmu.o \
	execute.o \
	env.o \
	tcache.o \
	\
	qmc/compile.o \
	qmc/qir.o \
	qmc/qir_opt.o \
	qmc/runtime_stubs.o \
	qmc/qcg/arch_traits.o \
	qmc/qcg/jitabi.o \
	qmc/qcg/qcg.o \
	qmc/qcg/qemit.o \
	qmc/qcg/qra.o \
	qmc/qcg/qsel.o \
	\
	guest/rv32_interp.o \
	guest/rv32_qir.o \
	main.o

OBJS := $(addprefix $(OUT)/, $(OBJS))
deps := $(OBJS:%.o=%.o.d)

BIN = $(OUT)/rv32jit

all: $(BIN)

.ONESHELL:
$(ASMJIT_DIR)/asmjit/asmjit.h:
	$(Q)git submodule update --init external/asmjit
	$(VECHO) "Please run 'make' again\n" && exit 1

$(OUT)/asmjit/core/%.o: $(ASMJIT_DIR)/asmjit/core/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/asmjit/x86/%.o: $(ASMJIT_DIR)/asmjit/x86/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/%.o: src/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/util/%.o: src/util/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/tcache/%.o: src/tcache/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/qmc/%.o: src/qmc/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/qmc/qcg/%.o: src/qmc/qcg/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/guest/%.o: src/guest/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

SHELL_HACK := $(shell mkdir -p $(OUT) $(OUT)/util $(OUT)/tcache $(OUT)/qmc $(OUT)/qmc/qcg $(OUT)/guest $(OUT)/asmjit/core $(OUT)/asmjit/x86)

$(OUT)/rv32jit: $(ASMJIT_DIR)/asmjit/asmjit.h $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) $(OBJS) $(LDFLAGS)

# Rules for downloading prebuilt RISC-V ELF files
include mk/external.mk
CHECK_ELF_FILES := \
	aes \
	nqueens \
	mandelbrot
check: $(BIN) $(aes_DATA) $(mandelbrot_DATA) $(nqueens_DATA)
	$(Q)$(foreach e,$(CHECK_ELF_FILES),\
	    $(PRINTF) "Running $(e).elf ...\n"; \
	    $(BIN) $(e).elf; \
	)

.PHONY: clean
clean:
	$(RM) $(BIN) $(OBJS) $(deps)

-include $(deps)
