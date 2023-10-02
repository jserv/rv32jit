# For each external target, the following must be defined in advance:
#   _DATA : the file to be read by specific executable.
#   _DATA_SHA1 : the checksum of the content in _DATA

COMMON_URL = https://github.com/sysprog21/rv32emu/raw/master/build

aes_DATA = aes.elf
aes_DATA_SHA1 = ba0f4d5a137152a60d3e19903a08401da0c5fff5

nqueens_DATA = nqueens.elf
nqueens_DATA_SHA1 = b5e0ae921af90871ae8ff7b2ff7a9f330f97b2e1

mandelbrot_DATA = mandelbrot.elf
mandelbrot_DATA_SHA1 = 5d21aad26d8a9f10fba88c6ed943024d58029011

define download
$($(T)_DATA):
	$(VECHO) "  GET\t$$@\n"
	$(Q)curl --progress-bar -O -L -C - "$(strip $(COMMON_URL)/$(T).elf)"
	$(Q)echo "$(strip $$($(T)_DATA_SHA1)) $$@" | $(SHA1SUM) -c
endef

EXTERNAL_DATA = aes nqueens mandelbrot
$(foreach T,$(EXTERNAL_DATA),$(eval $(download))) 
