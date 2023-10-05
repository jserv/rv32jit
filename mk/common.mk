UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    PRINTF = printf
else
    PRINTF = env printf
endif

# Detect clang versions
CXX = clang++
CXX := $(shell which $(CXX))
ifndef CXX
    # Try appending a suffix.
    CXX := clang++-15
    CXX := $(shell which $(CXX))
    ifndef CXX
        $(error "No clang++ found.")
    endif
endif
CXX_VERSION := $(shell $(CXX) --version | grep -oP 'clang version \K\d+')
ifneq ($(shell echo "$(CXX_VERSION) >= 15" | bc), 1)
    $(error The detected version of clang++ is $(CXX_VERSION). Upgrade to clang 15 or a later version.)
endif

# Control the build verbosity
ifeq ("$(VERBOSE)","1")
    Q :=
    VECHO = @true
    REDIR =
else
    Q := @
    VECHO = @$(PRINTF)
    REDIR = >/dev/null
endif

# Test suite
PASS_COLOR = \e[32;01m
NO_COLOR = \e[0m

notice = $(PRINTF) "$(PASS_COLOR)$(strip $1)$(NO_COLOR)\n"

# File utilities
SHA1SUM = sha1sum
SHA1SUM := $(shell which $(SHA1SUM))
ifndef SHA1SUM
    SHA1SUM = shasum
    SHA1SUM := $(shell which $(SHA1SUM))
    ifndef SHA1SUM
        $(warning No shasum found. Disable checksums)
        SHA1SUM := echo
    endif
endif
