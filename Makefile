include mk/common.mk

CXXFLAGS := -O2 -flto -Wall -Wextra -Wno-c99-designator
CXXFLAGS += -I src
CXXFLAGS += -std=gnu++20 -fno-rtti -fexceptions
LDFLAGS :=

OUT ?= build

# AsmJit
ASMJIT_DIR = external/asmjit/src
CXXFLAGS += -I $(ASMJIT_DIR)
CXXFLAGS += \
	-D ASMJIT_EMBED \
	-D ASMJIT_BUILD_RELEASE \
	-D ASMJIT_NO_LOGGING \
	-D ASMJIT_NO_DEPRECATED \
	-D ASMJIT_NO_AARCH32 -D ASMJIT_NO_AARCH64 \
	-D ASMJIT_NO_FOREIGN \
	-D ASMJIT_NO_COMPILER
ASMJIT_SRCS := \
        $(wildcard $(ASMJIT_DIR)/asmjit/core/*.cpp) \
        $(wildcard $(ASMJIT_DIR)/asmjit/x86/*.cpp)
LDFLAGS += -lrt

OBJS := $(patsubst $(ASMJIT_DIR)/%.cpp,%.o,$(ASMJIT_SRCS))

OBJS += \
	util/common.o \
	\
	arena.o \
	mmu.o \
	execute.o \
	env.o \
	tcache.o \
	runtime_stubs.o \
	\
	ir/compile.o \
	ir/qir.o \
	ir/qir_opt.o \
	\
	codegen/arch_traits.o \
	codegen/jitabi.o \
	codegen/qcg.o \
	codegen/emit.o \
	codegen/regalloc.o \
	codegen/select.o \
	\
	guest/rv32_interp.o \
	guest/rv32_qir.o \
	\
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

$(OUT)/ir/%.o: src/ir/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/codegen/%.o: src/codegen/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

$(OUT)/guest/%.o: src/guest/%.cpp
	$(VECHO) "  CXX\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) -c -MMD -MF $@.d $<

SHELL_HACK := $(shell mkdir -p $(OUT) $(OUT)/util $(OUT)/ir $(OUT)/codegen $(OUT)/guest $(OUT)/asmjit/core $(OUT)/asmjit/x86)

$(OUT)/rv32jit: $(ASMJIT_DIR)/asmjit/asmjit.h $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CXX) -o $@ $(CXXFLAGS) $(OBJS) $(LDFLAGS)

# Rules for downloading prebuilt RISC-V ELF files
include mk/external.mk
check: $(BIN) $(CHECK_ELF_FILES)
	$(Q)$(foreach e,$(CHECK_ELF_FILES),\
	    $(PRINTF) "Running $(e) ...\n"; \
	    $(BIN) $(e) && $(call notice, [OK]); \
	)

.PHONY: clean
clean:
	$(RM) $(BIN) $(OBJS) $(deps)

-include $(deps)
