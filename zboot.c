/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "esprom.h"
#include "esprtc.h"
#include "espgpio.h"
#include "zboot_util.h"
#include "zboot.h"
#include "zboot_private.h"
#include "zboot_hex2a.h"

#ifndef UART_CLK_FREQ
// reset apb freq = 2x crystal freq: http://esp8266-re.foogod.com/wiki/Serial_UART
#define UART_CLK_FREQ	(26000000 * 2)
#endif

//#define DEBUG(...)  ets_printf(__VA_ARGS__)
#define DEBUG(...)

// --------------------------------------------------------------------------------------------------
// Images 

static uint32_t check_image(uint32_t readpos)
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

// prevent this function being placed inline with main
// to keep main's stack size as small as possible
// don't mark as static or it'll be optimised out when
// using the assembler stub
uint32_t NOINLINE find_image(void)
{
   uint32_t runAddr;
   uint32_t flashsize;
   int32_t romToBoot;
   bool updateConfig = false;
   uint8_t buffer[SECTOR_SIZE];
   zboot_config *config = (zboot_config*)buffer;
#ifdef BOOT_GPIO_ENABLED
   bool gpio_boot = false;
#endif
   zboot_rtc_data rtc;
   uint8_t temp_boot = false;
   int i;

#ifdef BOOT_BAUDRATE
   // soft reset doesn't reset PLL/divider, so leave as configured
   if (get_reset_reason() != REASON_SOFT_RESTART)
      uart_div_modify( 0, UART_CLK_FREQ / BOOT_BAUDRATE);
#endif

#if defined BOOT_DELAY_MICROS && BOOT_DELAY_MICROS > 0
   // delay to slow boot (help see messages when debugging)
   ets_delay_us(BOOT_DELAY_MICROS);
#endif

   ets_printf("\n\nzboot v%u.%u.%u\n", ZBOOT_VERSION_MAJOR, ZBOOT_VERSION_MINOR,
      ZBOOT_VERSION_INCREMENTAL);
   esprom_get_flash_size(&flashsize);

   // Validate the zboot config
   SPIRead(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
   if(config->magic != ZBOOT_CONFIG_MAGIC)
   {
      ets_printf("Invalid zboot config magic\n");
      updateConfig = true;
   }
   else
   {
      uint8_t chksum = zboot_config_checksum(config);
      if(chksum != config->chksum)
      {
         ets_printf("zboot config checksum mismatch (%02x calculated, %02x expected)\n",
            chksum, config->chksum);
         updateConfig = true;
      }
   }
   if(updateConfig)
   {
      ets_printf("Writing default boot config.\n");
      default_config(config, flashsize);
      SPIEraseSector(BOOT_CONFIG_SECTOR);
      SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
   }
   updateConfig = false;

   // First ROM to try is the index stored in the zboot_config, unless a temporary
   //   ROM was selected in the non-volatie RTC memory
   romToBoot = config->current_rom;
   if (!rtc_copy_mem(ZBOOT_RTC_ADDR, &rtc, sizeof(rtc), false))
   {
      ets_printf("Failed to read RTC memory\n");
   }
   else if(rtc.chksum != zboot_rtc_checksum(&rtc))
   {
      ets_printf("RTC memory checksum failure\n");
   }
   else
   {
      if (rtc.next_mode & MODE_TEMP_ROM)
      {
         if (rtc.temp_rom >= config->count)
         {
            ets_printf("Invalid temp ROM selected (%u, %u max)\n", rtc.temp_rom, config->count);
         }
         else
         {
            ets_printf("Booting temporary ROM index %u\n", rtc.temp_rom);
            temp_boot = true;
            romToBoot = rtc.temp_rom;
         }
      }
   }

#if defined(BOOT_GPIO_ENABLED) || defined (BOOT_GPIO_SKIP_ENABLED)
   if(config->mode & MODE_GPIO_ROM != 0 && gpio_asserted(BOOT_GPIO_NUM))
   {
#if defined(BOOT_GPIO_ENABLED)
      if (config->gpio_rom >= config->count)
      {
         ets_printf("Invalid GPIO ROM selected.\r\n");
         return 0;
      }
      ets_printf("Booting GPIO-selected ROM.\r\n");
      romToBoot = config->gpio_rom;
      gpio_boot = true;
#elif defined(BOOT_GPIO_SKIP_ENABLED)
      romToBoot = config->current_rom + 1;
      if (romToBoot >= config->count)
          romToBoot = 0;
      config->current_rom = romToBoot;
#endif
      updateConfig = true;
      if (config->mode & MODE_GPIO_ERASES_SDKCONFIG)
      {
         uint8_t sec;
         ets_printf("Erasing SDK config sectors before booting.\r\n");
         for (sec = 1; sec < 5; sec++)
         {
            SPIEraseSector((flashsize / SECTOR_SIZE) - sec);
         }
      }
   }
#endif

   if (config->current_rom >= config->count)
   {
      // if invalid rom selected try rom 0
      ets_printf("Invalid ROM selected, defaulting to 0.\n");
      romToBoot = 0;
      config->current_rom = 0;
      updateConfig = true;
   }

   // check rom is valid
   DEBUG("Checking image %u @ %08x\n", romToBoot, config->roms[romToBoot]);
   runAddr = check_image(config->roms[romToBoot]);

#ifdef BOOT_GPIO_ENABLED
   if (gpio_boot && runAddr == 0)
   {
      // don't switch to backup for gpio-selected rom
      ets_printf("GPIO boot ROM (%d) is bad.\r\n", romToBoot);
      return 0;
   }
#endif
   if (temp_boot && runAddr == 0)
   {
      ets_printf("Temp boot ROM (%d) is bad.\r\n", romToBoot);
      rtc.next_mode = MODE_STANDARD; // make sure rtc temp boot mode doesn't persist
      rtc.chksum = zboot_rtc_checksum(&rtc);
      rtc_copy_mem(ZBOOT_RTC_ADDR, &rtc, sizeof(rtc), true);
      return 0;
   }

   // check we have a good rom
   while (runAddr == 0)
   {
      ets_printf("ROM %d (0x%08x) is bad.\r\n", romToBoot, config->roms[romToBoot]);
      // for normal mode try each previous rom
      // until we find a good one or run out
      updateConfig = true;
      romToBoot--;
      if (romToBoot < 0)
         romToBoot = config->count - 1;
      if (romToBoot == config->current_rom)
      {
         // tried them all and all are bad!
         ets_printf("No good ROM available.\r\n");
         return 0;
      }
      runAddr = check_image(config->roms[romToBoot]);
   }

   // re-write config, if required
   if (updateConfig)
   {
      ets_printf("Updating zboot config with ROM index %u\n", romToBoot);
      config->current_rom = romToBoot;
      config->chksum = zboot_config_checksum(config);
      SPIEraseSector(BOOT_CONFIG_SECTOR);
      SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
   }

   // set rtc boot data for app to read
   rtc.magic = ZBOOT_RTC_MAGIC;
   rtc.next_mode = MODE_STANDARD;
   rtc.last_mode = MODE_STANDARD;
   if (temp_boot)
      rtc.last_mode |= MODE_TEMP_ROM;
#ifdef BOOT_GPIO_ENABLED
   if (gpio_boot)
      rtc.last_mode |= MODE_GPIO_ROM;
#endif
   rtc.last_rom = romToBoot;
   rtc.temp_rom = 0;
   rtc.chksum = zboot_rtc_checksum(&rtc);
   rtc_copy_mem(ZBOOT_RTC_ADDR, &rtc, sizeof(zboot_rtc_data), true);

   ets_printf("Booting ROM %d @ 0x%08x\r\n", romToBoot, config->roms[romToBoot]);
   // copy the loader to top of iram
   ets_memcpy((void*)_text_addr, _text_data, _text_len);
   return runAddr; // return address to load from
}

#ifdef BOOT_NO_ASM

// small stub method to ensure minimum stack space used
void call_user_start(void)
{
   uint32_t addr;
   stage2a *loader;

   addr = find_image();
   if (addr != 0)
   {
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
