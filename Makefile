# Makefile for zboot
# https://github.com/zorxx/zboot

ZTOOL ?= ../ztool/ztool

ZBOOT_BUILD_BASE ?= build
ZBOOT_FW_BASE    ?= firmware

ifdef TOOLCHAIN_PREFIX
CC := $(TOOLCHAIN_PREFIX)gcc
else
CC := xtensa-lx106-elf-gcc
endif
LD := $(CC) 

ifdef XTENSA_BINDIR
CC := $(addprefix $(XTENSA_BINDIR)/,$(CC))
LD := $(addprefix $(XTENSA_BINDIR)/,$(LD))
endif

ifeq ($(V),1)
Q :=
else
Q := @
endif

CFLAGS = -Os -O3 -Wpointer-arith -Wundef -Werror -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH -DDEBUG=0 -D_GNU_SOURCE -I esp8266
LDFLAGS = -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static -Wl,-Map=$(ZBOOT_BUILD_BASE)/zboot.map
LD_SCRIPT = zboot.ld
VPATH := esp8266

ifneq ($(ZBOOT_DELAY_MICROS),)
	CFLAGS += -DBOOT_DELAY_MICROS=$(ZBOOT_DELAY_MICROS)
endif
ifneq ($(ZBOOT_BAUDRATE),)
	CFLAGS += -DBOOT_BAUDRATE=$(ZBOOT_BAUDRATE)
endif
ifeq ($(ZBOOT_GPIO_ENABLED),1)
	CFLAGS += -DBOOT_GPIO_ENABLED
endif
ifeq ($(ZBOOT_GPIO_SKIP_ENABLED),1)
	CFLAGS += -DBOOT_GPIO_SKIP_ENABLED
endif
ifneq ($(ZBOOT_GPIO_NUMBER),)
	CFLAGS += -DBOOT_GPIO_NUM=$(ZBOOT_GPIO_NUMBER)
endif
ifneq ($(ZBOOT_DEFAULT_CONFIG_IMAGE_COUNT),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_IMAGE_COUNT=$(ZBOOT_DEFAULT_CONFIG_IMAGE_COUNT)
endif
ifneq ($(ZBOOT_DEFAULT_CONFIG_ROM0),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM0=$(ZBOOT_DEFAULT_CONFIG_ROM0)
endif
ifneq ($(ZBOOT_DEFAULT_CONFIG_ROM1),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM1=$(ZBOOT_DEFAULT_CONFIG_ROM1)
endif
ifneq ($(ZBOOT_DEFAULT_CONFIG_ROM2),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM2=$(ZBOOT_DEFAULT_CONFIG_ROM2)
endif
ifneq ($(ZBOOT_DEFAULT_CONFIG_ROM3),)
	CFLAGS += -DBOOT_DEFAULT_CONFIG_ROM3=$(ZBOOT_DEFAULT_CONFIG_ROM3)
endif
ifneq ($(ZBOOT_EXTRA_INCDIR),)
	CFLAGS += $(addprefix -I,$(ZBOOT_EXTRA_INCDIR))
endif
CFLAGS += $(addprefix -I,.)

.SECONDARY:

ZBOOT_FILES := zboot.c zboot_util.c espgpio.c esprom.c esprtc.c #chip_boot.c spi_flash.c

all: $(ZBOOT_BUILD_BASE) $(ZBOOT_FW_BASE) $(ZBOOT_FW_BASE)/zboot.bin

$(ZBOOT_BUILD_BASE):
	$(Q) mkdir -p $@

$(ZBOOT_FW_BASE):
	$(Q) mkdir -p $@

$(ZBOOT_BUILD_BASE)/zboot.o: zboot.c zboot_private.h zboot.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -I$(ZBOOT_BUILD_BASE) -c $< -o $@

$(ZBOOT_BUILD_BASE)/%.o: %.c %.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(ZBOOT_BUILD_BASE)/%.elf: $(foreach file,$(ZBOOT_FILES),$(ZBOOT_BUILD_BASE)/$(file:.c=.o))
	@echo "LD $@"
	$(Q) $(LD) -T$(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $^ -Wl,--end-group -o $@

$(ZBOOT_FW_BASE)/%.bin: $(ZBOOT_BUILD_BASE)/%.elf
	@echo "ZB $@"
	$(Q) $(ZTOOL) -d 0 -b -c$(SPI_SIZE) -m$(SPI_MODE) -f$(SPI_SPEED) -e$< -o$@ -s".text .final .rodata"

clean:
	@echo "RM $(ZBOOT_BUILD_BASE) $(ZBOOT_FW_BASE)"
	$(Q) rm -rf $(ZBOOT_BUILD_BASE)
	$(Q) rm -rf $(ZBOOT_FW_BASE)
