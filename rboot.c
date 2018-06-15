//////////////////////////////////////////////////
// rBoot open source boot loader for ESP8266.
// Copyright 2015 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////

#include <stdbool.h>
#include "rboot-private.h"
#include <rboot-hex2a.h>

#ifndef UART_CLK_FREQ
// reset apb freq = 2x crystal freq: http://esp8266-re.foogod.com/wiki/Serial_UART
#define UART_CLK_FREQ	(26000000 * 2)
#endif

#define DEBUG(...)  ets_printf(__VA_ARGS__)

static uint32_t CheckImage(uint32_t readpos)
{
   uint8_t buffer[BUFFER_SIZE];
   zimage_header *zheader = (zimage_header *) buffer;   
   uint32_t start_pos = readpos;
   uint32_t value;
   uint32_t count;
   uint32_t i;
   uint32_t chksum = 0; 

   if (readpos == 0 || readpos == 0xffffffff)
   {
      DEBUG("Invalid section start address (%08x)\n", start_pos);
      return 0;
   }

   if (SPIRead(readpos, zheader, sizeof(*zheader)) != 0)
   {
      DEBUG("Failed to read header (%u bytes)\n", sizeof(*zheader));
      return 0;
   }
   readpos += sizeof(*zheader);

   // Sanity-check header 
   if(zheader->magic != ZBOOT_MAGIC)
   {
      DEBUG("Invalid header magic (%08x, expected %08x)\n", zheader->magic, ZBOOT_MAGIC);
      return 0;
   }
   if(zheader->count > 256)
   {
      DEBUG("Invalid section count (%u)\n", zheader->count);
      return 0;
   }
   if(zheader->entry < 0x40100000 || zheader->entry >= 0x40180000)
   {
      DEBUG("Invalid entrypoint (%08x)\n", zheader->entry);
      return 0;
   }

   // Add image header to checksum
   for (i = 0; i < sizeof(*zheader); i += sizeof(uint32_t))
      chksum += *((uint32_t *) (buffer + i));
   
   // test each section
   count = zheader->count;
   for (i = 0; i < count; ++i)
   {
      section_header *sect = (section_header *) buffer;
      uint32_t remaining;

      if (SPIRead(readpos, sect, sizeof(*sect)) != 0
      || (sect->length % sizeof(uint32_t) != 0))
      {
         DEBUG("Section %u, invalid length (%08x)\n", i, sect->length);
         return 0;
      }
      readpos += sizeof(*sect);

      // Add section header to checksum
      chksum += (uint32_t) sect->address;
      chksum += sect->length;

      remaining = sect->length;
      while (remaining > 0) 
      {
         uint32_t readlen = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
         uint32_t loop;

         if (SPIRead(readpos, buffer, readlen) != 0)
         {
            DEBUG("Failed to read section %u data at offset (%08x)\n", i, remaining);
            return 0;
         }
         readpos += readlen;
         remaining -= readlen;
         for (loop = 0; loop < readlen; loop += sizeof(uint32_t))
            chksum += *((uint32_t *) (buffer + loop));
      }
   }

   if (SPIRead(readpos, &value, sizeof(value)) != 0)
   {
      DEBUG("Failed to read checksum from flash\n"); 
      return 0;
   }

   if(value != chksum)
   {
      DEBUG("Checksum mismatch (calculated %08x, expected %08x))\n", chksum, value); 
      return 0;
   }

   return start_pos; 
}

#if defined (BOOT_GPIO_ENABLED) || defined(BOOT_GPIO_SKIP_ENABLED)

#if BOOT_GPIO_NUM > 16
#error "Invalid BOOT_GPIO_NUM value (disable BOOT_GPIO_ENABLED to disable this feature)"
#endif

// sample gpio code for gpio16
#define ETS_UNCACHED_ADDR(addr) (addr)
#define READ_PERI_REG(addr) (*((volatile uint32_t *)ETS_UNCACHED_ADDR(addr)))
#define WRITE_PERI_REG(addr, val) (*((volatile uint32_t *)ETS_UNCACHED_ADDR(addr))) = (uint32_t)(val)
#define PERIPHS_RTC_BASEADDR				0x60000700
#define REG_RTC_BASE  PERIPHS_RTC_BASEADDR
#define RTC_GPIO_OUT							(REG_RTC_BASE + 0x068)
#define RTC_GPIO_ENABLE							(REG_RTC_BASE + 0x074)
#define RTC_GPIO_IN_DATA						(REG_RTC_BASE + 0x08C)
#define RTC_GPIO_CONF							(REG_RTC_BASE + 0x090)
#define PAD_XPD_DCDC_CONF						(REG_RTC_BASE + 0x0A0)
static uint32_t get_gpio16(void) {
	// set output level to 1
	WRITE_PERI_REG(RTC_GPIO_OUT, (READ_PERI_REG(RTC_GPIO_OUT) & (uint32_t)0xfffffffe) | (uint32_t)(1));

	// read level
	WRITE_PERI_REG(PAD_XPD_DCDC_CONF, (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32_t)0x1);	// mux configuration for XPD_DCDC and rtc_gpio0 connection
	WRITE_PERI_REG(RTC_GPIO_CONF, (READ_PERI_REG(RTC_GPIO_CONF) & (uint32_t)0xfffffffe) | (uint32_t)0x0);	//mux configuration for out enable
	WRITE_PERI_REG(RTC_GPIO_ENABLE, READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32_t)0xfffffffe);	//out disable

	return (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1);
}

// support for "normal" GPIOs (other than 16)
#define REG_GPIO_BASE            0x60000300
#define GPIO_IN_ADDRESS          (REG_GPIO_BASE + 0x18)
#define GPIO_ENABLE_OUT_ADDRESS  (REG_GPIO_BASE + 0x0c)
#define REG_IOMUX_BASE           0x60000800
#define IOMUX_PULLUP_MASK        (1<<7)
#define IOMUX_FUNC_MASK          0x0130
const uint8_t IOMUX_REG_OFFS[] = {0x34, 0x18, 0x38, 0x14, 0x3c, 0x40, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x04, 0x08, 0x0c, 0x10};
const uint8_t IOMUX_GPIO_FUNC[] = {0x00, 0x30, 0x00, 0x30, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30};

static int get_gpio(int gpio_num) {
	// disable output buffer if set
	uint32_t old_out = READ_PERI_REG(GPIO_ENABLE_OUT_ADDRESS);
	uint32_t new_out = old_out & ~ (1<<gpio_num);
	WRITE_PERI_REG(GPIO_ENABLE_OUT_ADDRESS, new_out);

	// set GPIO function, enable soft pullup
	uint32_t iomux_reg = REG_IOMUX_BASE + IOMUX_REG_OFFS[gpio_num];
	uint32_t old_iomux = READ_PERI_REG(iomux_reg);
	uint32_t gpio_func = IOMUX_GPIO_FUNC[gpio_num];
	uint32_t new_iomux = (old_iomux & ~IOMUX_FUNC_MASK) | gpio_func | IOMUX_PULLUP_MASK;
	WRITE_PERI_REG(iomux_reg, new_iomux);

	// allow soft pullup to take effect if line was floating
	ets_delay_us(10);
	int result = READ_PERI_REG(GPIO_IN_ADDRESS) & (1<<gpio_num);

	// set iomux & GPIO output mode back to initial values
	WRITE_PERI_REG(iomux_reg, old_iomux);
	WRITE_PERI_REG(GPIO_ENABLE_OUT_ADDRESS, old_out);
	return (result ? 1 : 0);
}

// return '1' if we should do a gpio boot
static int perform_gpio_boot(rboot_config *romconf) {
	if (romconf->mode & MODE_GPIO_ROM == 0) {
		return 0;
	}

	// pin low == GPIO boot
	if (BOOT_GPIO_NUM == 16) {
		return (get_gpio16() == 0);
	} else {
		return (get_gpio(BOOT_GPIO_NUM) == 0);
	}
}

#endif

#ifdef BOOT_RTC_ENABLED
uint32_t system_rtc_mem(int32_t addr, void *buff, int32_t length, uint32_t mode) {

    int32_t blocks;

    // validate reading a user block
    if (addr < 64) return 0;
    if (buff == 0) return 0;
    // validate 4 byte aligned
    if (((uint32_t)buff & 0x3) != 0) return 0;
    // validate length is multiple of 4
    if ((length & 0x3) != 0) return 0;

    // check valid length from specified starting point
    if (length > (0x300 - (addr * 4))) return 0;

    // copy the data
    for (blocks = (length >> 2) - 1; blocks >= 0; blocks--) {
        volatile uint32_t *ram = ((uint32_t*)buff) + blocks;
        volatile uint32_t *rtc = ((uint32_t*)0x60001100) + addr + blocks;
		if (mode == RBOOT_RTC_WRITE) {
			*rtc = *ram;
		} else {
			*ram = *rtc;
		}
    }

    return 1;
}
#endif

#ifdef BOOT_BAUDRATE
static enum rst_reason get_reset_reason(void) {

	// reset reason is stored @ offset 0 in system rtc memory
	volatile uint32_t *rtc = (uint32_t*)0x60001100;

	return *rtc;
}
#endif

#if defined(BOOT_CONFIG_CHKSUM) || defined(BOOT_RTC_ENABLED)
// calculate checksum for block of data
// from start up to (but excluding) end
static uint8_t calc_chksum(uint8_t *start, uint8_t *end) {
	uint8_t chksum = CHKSUM_INIT;
	while(start < end) {
		chksum ^= *start;
		start++;
	}
	return chksum;
}
#endif

#ifndef BOOT_DEFAULT_CONFIG_IMAGE_COUNT
#define BOOT_DEFAULT_CONFIG_IMAGE_COUNT 2
#endif

#ifndef BOOT_DEFAULT_CONFIG_ROM0
#define BOOT_DEFAULT_CONFIG_ROM0 (SECTOR_SIZE * (BOOT_CONFIG_SECTOR + 1))
#endif

#ifndef BOOT_DEFAULT_CONFIG_ROM1
#define BOOT_DEFAULT_CONFIG_ROM1 ((flashsize / 2) + (SECTOR_SIZE * (BOOT_CONFIG_SECTOR + 1)))
#endif

#ifndef BOOT_DEFAULT_CONFIG_ROM2
#define BOOT_DEFAULT_CONFIG_ROM2 0
#endif

#ifndef BOOT_DEFAULT_CONFIG_ROM3
#define BOOT_DEFAULT_CONFIG_ROM3 0
#endif

// populate the user fields of the default config
// created on first boot or in case of corruption
static uint8_t default_config(rboot_config *romconf, uint32_t flashsize) {
	romconf->count =  BOOT_DEFAULT_CONFIG_IMAGE_COUNT;
	romconf->roms[0] = BOOT_DEFAULT_CONFIG_ROM0;
	romconf->roms[1] = BOOT_DEFAULT_CONFIG_ROM1;
	romconf->roms[2] = BOOT_DEFAULT_CONFIG_ROM2;
	romconf->roms[3] = BOOT_DEFAULT_CONFIG_ROM3;
#ifdef BOOT_GPIO_ENABLED
	romconf->mode = MODE_GPIO_ROM;
#endif
#ifdef BOOT_GPIO_SKIP_ENABLED
	romconf->mode = MODE_GPIO_SKIP;
#endif
}

// prevent this function being placed inline with main
// to keep main's stack size as small as possible
// don't mark as static or it'll be optimised out when
// using the assembler stub
uint32_t NOINLINE find_image(void) {

	uint8_t flag;
	uint32_t runAddr;
	uint32_t flashsize;
	int32_t romToBoot;
	bool updateConfig = false;
	uint8_t buffer[SECTOR_SIZE];
#ifdef BOOT_GPIO_ENABLED
	bool gpio_boot = false;
#endif
#if defined (BOOT_GPIO_ENABLED) || defined(BOOT_GPIO_SKIP_ENABLED)
	uint8_t sec;
#endif
#ifdef BOOT_RTC_ENABLED
	rboot_rtc_data rtc;
	uint8_t temp_boot = false;
#endif

	rboot_config *romconf = (rboot_config*)buffer;
	rom_header *header = (rom_header*)buffer;

#ifdef BOOT_BAUDRATE
	// soft reset doesn't reset PLL/divider, so leave as configured
	if (get_reset_reason() != REASON_SOFT_RESTART) {
		uart_div_modify( 0, UART_CLK_FREQ / BOOT_BAUDRATE);
	}
#endif

#if defined BOOT_DELAY_MICROS && BOOT_DELAY_MICROS > 0
	// delay to slow boot (help see messages when debugging)
	ets_delay_us(BOOT_DELAY_MICROS);
#endif

	ets_printf("\r\n\r\nrBoot v2.0.0 - richardaburton@gmail.com, zorxx@zorxx.com\r\n");

	// read rom header
	SPIRead(0, header, sizeof(rom_header));

	// print and get flash size
	ets_printf("Flash: ");
	flag = header->flags2 >> 4;
	if (flag == 0) {
		ets_printf("4");
		flashsize = 0x80000;
	} else if (flag == 1) {
		ets_printf("2");
		flashsize = 0x40000;
	} else if (flag == 2) {
		ets_printf("8");
		flashsize = 0x100000;
	} else if (flag == 3) {
		ets_printf("16");
#ifdef BOOT_BIG_FLASH
		flashsize = 0x200000;
#else
		flashsize = 0x100000; // limit to 8Mbit
#endif
	} else if (flag == 4) {
		ets_printf("32");
#ifdef BOOT_BIG_FLASH
		flashsize = 0x400000;
#else
		flashsize = 0x100000; // limit to 8Mbit
#endif
	} else {
		ets_printf("unknown");
		// assume at least 4mbit
		flashsize = 0x80000;
	}
        ets_printf(" Mbit, ");

	// print spi mode
	if (header->flags1 == 0) {
		ets_printf("QIO, ");
	} else if (header->flags1 == 1) {
		ets_printf("QOUT, ");
	} else if (header->flags1 == 2) {
		ets_printf("DIO, ");
	} else if (header->flags1 == 3) {
		ets_printf("DOUT, ");
	} else {
		ets_printf("unknown mode, ");
	}

	// print spi speed
	flag = header->flags2 & 0x0f;
	if (flag == 0) ets_printf("40");
	else if (flag == 1) ets_printf("26.7");
	else if (flag == 2) ets_printf("20");
	else if (flag == 0x0f) ets_printf("80");
	else ets_printf("unknown speed");
        ets_printf(" MHz\r\nOption: ");

	// print enabled options
#ifdef BOOT_BIG_FLASH
	ets_printf("big_flash ");
#endif
#ifdef BOOT_CONFIG_CHKSUM
	ets_printf("config_chksum ");
#endif
#ifdef BOOT_GPIO_ENABLED
	ets_printf("gpio_rom_mode(%d) ", BOOT_GPIO_NUM);
#endif
#ifdef BOOT_GPIO_SKIP_ENABLED
	ets_printf("gpio_skip_mode(%d) ", BOOT_GPIO_NUM);
#endif
#ifdef BOOT_RTC_ENABLED
	ets_printf("rtc_data ");
#endif
	ets_printf("\r\n");

	// read boot config
	SPIRead(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
	// fresh install or old version?
	if (romconf->magic != BOOT_CONFIG_MAGIC || romconf->version != BOOT_CONFIG_VERSION
#ifdef BOOT_CONFIG_CHKSUM
		|| romconf->chksum != calc_chksum((uint8_t*)romconf, (uint8_t*)&romconf->chksum)
#endif
		) {
		// create a default config for a standard 2 rom setup
		ets_printf("Writing default boot config.\r\n");
		ets_memset(romconf, 0x00, sizeof(rboot_config));
		romconf->magic = BOOT_CONFIG_MAGIC;
		romconf->version = BOOT_CONFIG_VERSION;
		default_config(romconf, flashsize);
#ifdef BOOT_CONFIG_CHKSUM
		romconf->chksum = calc_chksum((uint8_t*)romconf, (uint8_t*)&romconf->chksum);
#endif
		// write new config sector
		SPIEraseSector(BOOT_CONFIG_SECTOR);
		SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
	}

	// try rom selected in the config, unless overriden by gpio/temp boot
	romToBoot = romconf->current_rom;

#ifdef BOOT_RTC_ENABLED
	// if rtc data enabled, check for valid data
	if (system_rtc_mem(RBOOT_RTC_ADDR, &rtc, sizeof(rboot_rtc_data), RBOOT_RTC_READ) &&
		(rtc.chksum == calc_chksum((uint8_t*)&rtc, (uint8_t*)&rtc.chksum))) {

		if (rtc.next_mode & MODE_TEMP_ROM) {
			if (rtc.temp_rom >= romconf->count) {
				ets_printf("Invalid temp ROM selected.\r\n");
				return 0;
			}
			ets_printf("Booting temp ROM.\r\n");
			temp_boot = true;
			romToBoot = rtc.temp_rom;
		}
	}
#endif

#if defined(BOOT_GPIO_ENABLED) || defined (BOOT_GPIO_SKIP_ENABLED)
	if (perform_gpio_boot(romconf)) {
#if defined(BOOT_GPIO_ENABLED)
		if (romconf->gpio_rom >= romconf->count) {
			ets_printf("Invalid GPIO ROM selected.\r\n");
			return 0;
		}
		ets_printf("Booting GPIO-selected ROM.\r\n");
		romToBoot = romconf->gpio_rom;
		gpio_boot = true;
#elif defined(BOOT_GPIO_SKIP_ENABLED)
		romToBoot = romconf->current_rom + 1;
		if (romToBoot >= romconf->count) {
			romToBoot = 0;
		}
		romconf->current_rom = romToBoot;
#endif
		updateConfig = true;
		if (romconf->mode & MODE_GPIO_ERASES_SDKCONFIG) {
			ets_printf("Erasing SDK config sectors before booting.\r\n");
			for (sec = 1; sec < 5; sec++) {
				SPIEraseSector((flashsize / SECTOR_SIZE) - sec);
			}
		}
	}
#endif

	// check valid rom number
	// gpio/temp boots will have already validated this
	if (romconf->current_rom >= romconf->count) {
		// if invalid rom selected try rom 0
		ets_printf("Invalid ROM selected, defaulting to 0.\r\n");
		romToBoot = 0;
		romconf->current_rom = 0;
		updateConfig = true;
	}

	// check rom is valid
        DEBUG("Checking image %u @ %08x\n", romToBoot, romconf->roms[romToBoot]);
	runAddr = CheckImage(romconf->roms[romToBoot]);

#ifdef BOOT_GPIO_ENABLED
	if (gpio_boot && runAddr == 0) {
		// don't switch to backup for gpio-selected rom
		ets_printf("GPIO boot ROM (%d) is bad.\r\n", romToBoot);
		return 0;
	}
#endif
#ifdef BOOT_RTC_ENABLED
	if (temp_boot && runAddr == 0) {
		// don't switch to backup for temp rom
		ets_printf("Temp boot ROM (%d) is bad.\r\n", romToBoot);
		// make sure rtc temp boot mode doesn't persist
		rtc.next_mode = MODE_STANDARD;
		rtc.chksum = calc_chksum((uint8_t*)&rtc, (uint8_t*)&rtc.chksum);
		system_rtc_mem(RBOOT_RTC_ADDR, &rtc, sizeof(rboot_rtc_data), RBOOT_RTC_WRITE);
		return 0;
	}
#endif

	// check we have a good rom
	while (runAddr == 0) {
		ets_printf("ROM %d (0x%08x) is bad.\r\n", romToBoot, romconf->roms[romToBoot]);
		// for normal mode try each previous rom
		// until we find a good one or run out
		updateConfig = true;
		romToBoot--;
		if (romToBoot < 0) romToBoot = romconf->count - 1;
		if (romToBoot == romconf->current_rom) {
			// tried them all and all are bad!
			ets_printf("No good ROM available.\r\n");
			return 0;
		}
		runAddr = CheckImage(romconf->roms[romToBoot]);
	}

	// re-write config, if required
	if (updateConfig) {
		romconf->current_rom = romToBoot;
#ifdef BOOT_CONFIG_CHKSUM
		romconf->chksum = calc_chksum((uint8_t*)romconf, (uint8_t*)&romconf->chksum);
#endif
		SPIEraseSector(BOOT_CONFIG_SECTOR);
		SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
	}

#ifdef BOOT_RTC_ENABLED
	// set rtc boot data for app to read
	rtc.magic = RBOOT_RTC_MAGIC;
	rtc.next_mode = MODE_STANDARD;
	rtc.last_mode = MODE_STANDARD;
	if (temp_boot) rtc.last_mode |= MODE_TEMP_ROM;
#ifdef BOOT_GPIO_ENABLED
	if (gpio_boot) rtc.last_mode |= MODE_GPIO_ROM;
#endif
	rtc.last_rom = romToBoot;
	rtc.temp_rom = 0;
	rtc.chksum = calc_chksum((uint8_t*)&rtc, (uint8_t*)&rtc.chksum);
	system_rtc_mem(RBOOT_RTC_ADDR, &rtc, sizeof(rboot_rtc_data), RBOOT_RTC_WRITE);
#endif

	ets_printf("Booting ROM %d @ 0x%08x\r\n", romToBoot, romconf->roms[romToBoot]);
	// copy the loader to top of iram
	ets_memcpy((void*)_text_addr, _text_data, _text_len);
	// return address to load from
	return runAddr;
}

#ifdef BOOT_NO_ASM

// small stub method to ensure minimum stack space used
void call_user_start(void) {
	uint32_t addr;
	stage2a *loader;

	addr = find_image();
	if (addr != 0) {
		loader = (stage2a*)entry_addr;
		loader(addr);
	}
}

#else

// assembler stub uses no stack space
// works with gcc
void call_user_start(void) {
	__asm volatile (
		"mov a15, a0\n"          // store return addr, hope nobody wanted a15!
		"call0 find_image\n"     // find a good rom to boot
		"mov a0, a15\n"          // restore return addr
		"bnez a2, 1f\n"          // ?success
		"ret\n"                  // no, return
		"1:\n"                   // yes...
		"movi a3, entry_addr\n"  // get pointer to entry_addr
		"l32i a3, a3, 0\n"       // get value of entry_addr
		"jx a3\n"                // now jump to it
	);
}

#endif
