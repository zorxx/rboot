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
   if(zheader->magic != ZIMAGE_MAGIC)
   {
      DEBUG("Invalid header magic (%08x, expected %08x)\n", zheader->magic, ZIMAGE_MAGIC);
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
   uint8_t bootIndex;
   uint8_t bootMode;
   bool updateConfig = false;
   uint8_t buffer[SECTOR_SIZE];
   zboot_config *config = (zboot_config*)buffer;
   zboot_rtc_data rtc;
   int i;

#ifdef BOOT_BAUDRATE
   // soft reset doesn't reset PLL/divider, so leave as configured
   if (get_reset_reason() != REASON_SOFT_RESTART)
      uart_div_modify(0, UART_CLK_FREQ / BOOT_BAUDRATE);
#endif

#if defined BOOT_DELAY_MICROS && BOOT_DELAY_MICROS > 0
   // delay to slow boot (help see messages when debugging)
   ets_delay_us(BOOT_DELAY_MICROS);
#endif

   ets_printf("\n\nzboot v%u.%u.%u\n", ZBOOT_VERSION_MAJOR, ZBOOT_VERSION_MINOR,
      ZBOOT_VERSION_INCREMENTAL);
   esprom_get_flash_size(&flashsize);

   // Read the zboot config from flash 
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

   bootIndex = config->current_rom;
   bootMode = MODE_STANDARD;

   // Read RTC memory to determine if we've done a warm reset and
   //  a temporary ROM was selected
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
      if (rtc.next_mode == MODE_TEMP_ROM)
      {
         if (rtc.next_rom >= config->count)
         {
            ets_printf("Invalid temp ROM selected (%u, %u max)\n", rtc.next_rom, config->count);
         }
         else
         {
            ets_printf("Booting temporary ROM index %u\n", rtc.next_rom);
            bootMode = MODE_TEMP_ROM; 
            bootIndex = rtc.next_rom;
         }
      }
   }

   if(bootMode == MODE_STANDARD)
   {
      switch(config->mode)
      {
         case MODE_GPIO_ROM:
            if(gpio_asserted(config->gpio_num))
            {
               if(config->gpio_rom < config->count)
               {
                  ets_printf("Invalid GPIO ROM selected.\r\n");
               }
               else
               {
                  bootIndex = config->gpio_rom;
                  ets_printf("Booting GPIO-selected ROM index %u\n", bootIndex);
                  bootMode = MODE_GPIO_ROM;
               }
            }
            break;

         case MODE_GPIO_SKIP:
            if(gpio_asserted(config->gpio_num))
            {
               bootIndex = config->current_rom + 1;
               if(bootIndex >= config->count)
                  bootIndex = 0;
               ets_printf("Booting GPIO-skip ROM index %u\n", bootIndex);
               bootMode = MODE_GPIO_SKIP;
            }
            break;
         default:
            ets_printf("Unsupported mode %u\n", config->mode);
            break;
      }
   }

   if (config->current_rom >= config->count)
   {
      ets_printf("Invalid ROM selected, defaulting to 0.\n");
      bootIndex = 0;
   }

   // Loop through all ROMs, strting with the selected one
   for(runAddr = 0, i = 0; runAddr == 0 && i < config->count; ++i)
   {
      uint8_t tryIndex = bootIndex + i;
      uint32_t tryAddress; 

      if(tryIndex >= config->count)
         tryIndex = 0;
      tryAddress = config->roms[tryIndex];
      DEBUG("Checking image %u @ %08x\n", tryIndex, tryAddress); 

      runAddr = check_image(tryAddress);
      if(0 == runAddr)
      {
         ets_printf("ROM %u is bad.\r\n", tryIndex); 
      }
      else
         bootIndex = tryIndex;
   }

   if(0 == runAddr)
   {
      ets_printf("No good ROM available.\r\n");
      return 0;
   }

   if(config->options & OPTION_GPIO_ERASES_SDKCONFIG)
   {
      uint8_t sec;
      ets_printf("Erasing SDK config sectors before booting.\r\n");
      for (sec = 1; sec < 5; sec++)
      {
         SPIEraseSector((flashsize / SECTOR_SIZE) - sec);
      }
   }

   // re-write config, if required
   if((config->options & OPTION_UPDATE_BOOT_INDEX) &&
      (config->mode != MODE_TEMP_ROM) &&
      (bootIndex != config->current_rom))
   {
      ets_printf("Updating zboot config with ROM index %u\n", bootIndex);
      config->current_rom = bootIndex;
      config->chksum = zboot_config_checksum(config);
      SPIEraseSector(BOOT_CONFIG_SECTOR);
      SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
   }

   // set rtc boot data for app to read
   rtc.magic = ZBOOT_RTC_MAGIC;
   rtc.next_mode = MODE_STANDARD;
   rtc.last_mode = bootMode; 
   rtc.last_rom = bootIndex; 
   rtc.rom_addr = config->roms[bootIndex];
   rtc.next_rom = 0;
   rtc.chksum = zboot_rtc_checksum(&rtc);
   rtc_copy_mem(ZBOOT_RTC_ADDR, &rtc, sizeof(zboot_rtc_data), true);

   ets_printf("Booting ROM %d @ 0x%08x\r\n", bootIndex, config->roms[bootIndex]);
   ets_memcpy((void*)_text_addr, _text_data, _text_len); // Copy second stage loader top of IRAM 
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
