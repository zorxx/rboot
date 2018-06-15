#
# Makefile for rBoot
# https://github.com/zorxx/zboot
#

ZTOOL ?= ../ztool/ztool

RBOOT_BUILD_BASE ?= build
RBOOT_FW_BASE    ?= firmware

ifndef XTENSA_BINDIR
CC := xtensa-lx106-elf-gcc
LD := xtensa-lx106-elf-gcc
else
CC := $(addprefix $(XTENSA_BINDIR)/,xtensa-lx106-elf-gcc)
LD := $(addprefix $(XTENSA_BINDIR)/,xtensa-lx106-elf-gcc)
endif

ifeq ($(V),1)
Q :=
else
Q := @
endif

CFLAGS    = -Os -O3 -Wpointer-arith -Wundef -Werror -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH
LDFLAGS   = -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static -Wl,-Map=$(RBOOT_BUILD_BASE)/rboot.map
LD_SCRIPT = eagle.app.v6.ld

ifeq ($(RBOOT_BIG_FLASH),1)
	CFLAGS += -DBOOT_BIG_FLASH
endif
ifneq ($(RBOOT_DELAY_MICROS),)
	CFLAGS += -DBOOT_DELAY_MICROS=$(RBOOT_DELAY_MICROS)
endif
ifneq ($(RBOOT_BAUDRATE),)
	CFLAGS += -DBOOT_BAUDRATE=$(RBOOT_BAUDRATE)
endif
ifeq ($(RBOOT_INTEGRATION),1)
	CFLAGS += -DRBOOT_INTEGRATION
endif
ifeq ($(RBOOT_RTC_ENABLED),1)
	CFLAGS += -DBOOT_RTC_ENABLED
endif
ifeq ($(RBOOT_CONFIG_CHKSUM),1)
	CFLAGS += -DBOOT_CONFIG_CHKSUM
endif
ifeq ($(RBOOT_GPIO_ENABLED),1)
	CFLAGS += -DBOOT_GPIO_ENABLED
endif
ifeq ($(RBOOT_GPIO_SKIP_ENABLED),1)
	CFLAGS += -DBOOT_GPIO_SKIP_ENABLED
endif
ifneq ($(RBOOT_GPIO_NUMBER),)
	CFLAGS += -DBOOT_GPIO_NUM=$(RBOOT_GPIO_NUMBER)
endif
ifneq ($(RBOOT_DEFAULT_CONFIG_IMAGE_COUNT),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_IMAGE_COUNT=$(RBOOT_DEFAULT_CONFIG_IMAGE_COUNT)
endif
ifneq ($(RBOOT_DEFAULT_CONFIG_ROM0),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM0=$(RBOOT_DEFAULT_CONFIG_ROM0)
endif
ifneq ($(RBOOT_DEFAULT_CONFIG_ROM1),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM1=$(RBOOT_DEFAULT_CONFIG_ROM1)
endif
ifneq ($(RBOOT_DEFAULT_CONFIG_ROM2),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM2=$(RBOOT_DEFAULT_CONFIG_ROM2)
endif
ifneq ($(RBOOT_DEFAULT_CONFIG_ROM3),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM3=$(RBOOT_DEFAULT_CONFIG_ROM3)
endif
ifneq ($(RBOOT_EXTRA_INCDIR),)
	CFLAGS += $(addprefix -I,$(RBOOT_EXTRA_INCDIR))
endif
CFLAGS += $(addprefix -I,.)

.SECONDARY:

#all: $(RBOOT_BUILD_BASE) $(RBOOT_FW_BASE) $(RBOOT_FW_BASE)/rboot.bin $(RBOOT_FW_BASE)/testload1.bin $(RBOOT_FW_BASE)/testload2.bin
all: $(RBOOT_BUILD_BASE) $(RBOOT_FW_BASE) $(RBOOT_FW_BASE)/rboot.bin

$(RBOOT_BUILD_BASE):
	$(Q) mkdir -p $@

$(RBOOT_FW_BASE):
	$(Q) mkdir -p $@

$(RBOOT_BUILD_BASE)/rboot-stage2a.o: rboot-stage2a.c rboot-private.h rboot.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(RBOOT_BUILD_BASE)/rboot-stage2a.elf: $(RBOOT_BUILD_BASE)/rboot-stage2a.o
	@echo "LD $@"
	$(Q) $(LD) -Trboot-stage2a.ld $(LDFLAGS) -Wl,--start-group $^ -Wl,--end-group -o $@

$(RBOOT_BUILD_BASE)/rboot-hex2a.h: $(RBOOT_BUILD_BASE)/rboot-stage2a.elf
	@echo "ZB $@"
	$(Q) $(ZTOOL) -d0 -i -e $< -o $@ -s ".text"

$(RBOOT_BUILD_BASE)/rboot.o: rboot.c rboot-private.h rboot.h $(RBOOT_BUILD_BASE)/rboot-hex2a.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -I$(RBOOT_BUILD_BASE) -c $< -o $@

$(RBOOT_BUILD_BASE)/%.o: %.c %.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(RBOOT_BUILD_BASE)/%.elf: $(RBOOT_BUILD_BASE)/%.o
	@echo "LD $@"
	$(Q) $(LD) -T$(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $^ -Wl,--end-group -o $@

$(RBOOT_FW_BASE)/%.bin: $(RBOOT_BUILD_BASE)/%.elf
	@echo "ZB $@"
	$(Q) $(ZTOOL) -d0 -b -c$(SPI_SIZE) -m$(SPI_MODE) -f$(SPI_SPEED) -e $< -o $@ -s".text .rodata"

clean:
	@echo "RM $(RBOOT_BUILD_BASE) $(RBOOT_FW_BASE)"
	$(Q) rm -rf $(RBOOT_BUILD_BASE)
	$(Q) rm -rf $(RBOOT_FW_BASE)
