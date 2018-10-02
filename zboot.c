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

#ifndef UART_CLK_FREQ
// reset apb freq = 2x crystal freq: http://esp8266-re.foogod.com/wiki/Serial_UART
#define UART_CLK_FREQ	(26000000 * 2)
#endif

#if defined(ZBOOT_DEBUG)
#define DBG(...)  ets_printf(__VA_ARGS__)
#define DBGCHAR(ch) ets_putc(ch); ets_putc('\r'); ets_putc('\n');
#else
#define DBG(...)
#define DBGCHAR(ch)
#endif

extern int _bss_start;
extern int _bss_end;

// BSS Data
uint32_t app_entrypoint;
uint32_t app_flash_base; 
uint32_t CacheEnable;
uint8_t buffer[BUFFER_SIZE];
zboot_rtc_data rtc;
zboot_config config;
zimage_header zheader;

extern void Cache_Read_Enable(uint8_t, uint8_t, uint8_t);

void __attribute__((section(".final.text"))) load_rom(uint32_t start_addr)
{
   uint32_t readpos = start_addr;
   uint8_t sectcount;
   uint8_t *writepos;
   uint32_t remaining;
   uint32_t header_count;
   uint32_t header_entry;
   section_header section;

   // read rom header
   readpos += sizeof(uint32_t); // skip magic
   SPIRead(readpos, &header_count, sizeof(header_count));
   readpos += sizeof(uint32_t);
   SPIRead(readpos, &header_entry, sizeof(header_entry));
   readpos += 28 * sizeof(uint32_t);

   // copy all the sections
   for(sectcount = header_count; sectcount > 0; sectcount--)
   {
      // read section header
      SPIRead(readpos, &section, sizeof(section_header));
      readpos += sizeof(section_header);

      // get section address and length
      writepos = (uint8_t *)section.address;
      remaining = section.length;

      if(0 == writepos)
      {
         readpos += remaining;
         continue;
      }

      while (remaining > 0)
      {
         uint32_t readlen = (remaining < SECTOR_SIZE) ? remaining : SECTOR_SIZE;
         SPIRead(readpos, writepos, readlen);
         readpos += readlen;
         writepos += readlen;
         remaining -= readlen;
      }
   }

   // Copy data to BSS so they're accessible via inline assembly
   app_entrypoint = header_entry;
   app_flash_base = start_addr;
   CacheEnable = (uint32_t) &Cache_Read_Enable;

   __asm__ __volatile__(
      "movi a0, 0x40100000\n"     // Start using the new vector table
      "wsr  a0, vecbase\n"

      "movi a5, app_flash_base\n" // Get the application offset in flash
      "l32i a5, a5, 0\n"
      "memw\n"

      "movi a4, 1\n"              // Cache_Read_Enable parameter 3 = 1 (32kB cache size)

      "addi a2, a5, 0\n"          // Cache_Read_Enable parameter 1 = (app_flash_base >> 20) & 1
      "srai a2, a2, 20\n"
      "and  a2, a2, a4\n"

      "addi a3, a5, 0\n"          // Cache_Read_Enable parameter 2 = (app_flash_base >> 21) & 1
      "srai a3, a3, 21\n"
      "and  a3, a3, a4\n"

      "movi a0, app_entrypoint\n" // Get the application entrypoint into a0
      "l32i a0, a0, 0\n"
      "memw\n"

      "movi a1, 0x40000000\n"     // Reset the stack pointer; point of no return

      // Jump to Cache_Read_Enable (don't make a function call).
      //  Cache_Read_Enable will be "tricked into" calling the application
      //  entrypoint when it executes the return operation since we've loaded
      //  a0 with the application entrypoint. This means that the cache will be
      //  enabled when the application begins execution, so the application
      //  entrypoint can be located in IRAM or flash.
      "movi a5, CacheEnable\n" // Get the address of Cache_Read_Enable
      "l32i a5, a5, 0\n"
      "memw\n"
      "jx a5\n"
   : : :"memory");

   // Shouldn't ever get here
}

// -------------------------------------------------------------------------------------------------
// Images 

static uint32_t check_image(uint32_t readpos)
{
   uint32_t value;
   uint32_t i;
   uint32_t chksum = 0; 

   if(readpos == 0 || readpos == 0xffffffff)
   {
      DBG("Invalid section start address (%08x)\n", readpos);
      return 0;
   }

   if(SPIRead(readpos, (void *) &zheader, sizeof(zheader)) != 0)
   {
      DBG("Failed to read header (%u bytes)\n", sizeof(zheader));
      return 0;
   }
   readpos += sizeof(zheader);

   // Sanity-check header 
   if(zheader.magic != ZIMAGE_MAGIC)
   {
      DBG("Invalid header magic (%08x, expected %08x)\n", zheader.magic, ZIMAGE_MAGIC);
      return 0;
   }
   if(zheader.count > 256)
   {
      DBG("Invalid section count (%u)\n", zheader.count);
      return 0;
   }
   if(zheader.entry < 0x40100000 || zheader.entry >= 0x40300000)
   {
      DBG("Invalid entrypoint (%08x)\n", zheader.entry);
      return 0;
   }

   // Add image header to checksum
   for(i = 0; i < sizeof(zheader); i += sizeof(uint32_t))
      chksum += *((uint32_t *) (((uint8_t *)&zheader) + i));
   
   // test each section
   DBG("Calculating checksum of %u sections\n", zheader.count);
   for(i = 0; i < zheader.count; ++i)
   {
      section_header sect;
      uint32_t remaining;

      if(SPIRead(readpos, &sect, sizeof(sect)) != 0
      || (sect.length % sizeof(uint32_t) != 0))
      {
         DBG("Section %u, invalid length (%08x)\n", i, sect.length);
         return 0;
      }
      readpos += sizeof(sect);

      // Add section header to checksum
      DBG("Section %u: Address 0x%08x, length 0x%08x\n",
         i, (uint32_t) sect.address, sect.length);
      chksum += (uint32_t) sect.address;
      chksum += sect.length;

      remaining = sect.length;
      while(remaining > 0)
      {
         uint32_t readlen = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
         uint32_t loop;

         if(SPIRead(readpos, buffer, readlen) != 0)
         {
            DBG("Failed to read section %u data at offset (%08x)\n", i, remaining);
            return 0;
         }
         readpos += readlen;
         remaining -= readlen;
         for(loop = 0; loop < readlen; loop += sizeof(uint32_t))
            chksum += *((uint32_t *) (buffer + loop));
      }
   }

   if(SPIRead(readpos, &value, sizeof(value)) != 0)
   {
      DBG("Failed to read checksum from flash\n"); 
      return 0;
   }

   if(value != chksum)
   {
      DBG("Checksum mismatch (calculated %08x, expected %08x))\n", chksum, value); 
      return 0;
   }

   return zheader.entry;
}

static void calculate_frst_index(uint8_t *index, uint8_t *mode)
{
   uint8_t bootIndex = config.current_rom;
   uint8_t bootMode = ZBOOT_MODE_STANDARD;

   // Read RTC memory to determine if we've done a warm reset and
   //  a temporary ROM was selected
   if(!rtc_copy_mem(ZBOOT_RTC_ADDR, &rtc, sizeof(rtc), false))
   {
      ets_printf("Failed to read RTC memory\n");
   }
   else if(rtc.chksum != zboot_rtc_checksum(&rtc))
   {
      ets_printf("RTC memory checksum failure\n");
   }
   else
   {
      if(rtc.next_mode == ZBOOT_MODE_TEMP_ROM)
      {
         if(rtc.next_rom >= config.count)
         {
            ets_printf("Invalid temp ROM selected (%u, %u max)\n", rtc.next_rom, config.count);
         }
         else
         {
            ets_printf("Booting temporary ROM index %u\n", rtc.next_rom);
            bootMode = ZBOOT_MODE_TEMP_ROM;
            bootIndex = rtc.next_rom;
         }
      }
   }

   if(bootMode == ZBOOT_MODE_STANDARD)
   {
      switch(config.mode)
      {
         case ZBOOT_MODE_GPIO_ROM:
            if(gpio_asserted(config.gpio_num))
            {
               if(config.gpio_rom < config.count)
               {
                  ets_printf("Invalid GPIO ROM selected.\r\n");
               }
               else
               {
                  bootIndex = config.gpio_rom;
                  ets_printf("Booting GPIO-selected ROM index %u\n", bootIndex);
                  bootMode = ZBOOT_MODE_GPIO_ROM;
               }
            }
            break;

         case ZBOOT_MODE_GPIO_SKIP:
            if(gpio_asserted(config.gpio_num))
            {
               bootIndex = config.current_rom + 1;
               if(bootIndex >= config.count)
                  bootIndex = 0;
               ets_printf("Booting GPIO-skip ROM index %u\n", bootIndex);
               bootMode = ZBOOT_MODE_GPIO_SKIP;
            }
            break;
         default:
            ets_printf("Unsupported mode %u\n", config.mode);
            break;
      }
   }

   if(config.current_rom >= config.count)
   {
      ets_printf("Invalid ROM selected, defaulting to 0.\n");
      bootIndex = 0;
   }

   *index = bootIndex;
   *mode = bootMode;
}

// -------------------------------------------------------------------------------------------------
// Entry

typedef void (*app_entry)(uint32_t);

void zboot_main(void)
{
   uint32_t runAddr;
   uint32_t flashSize;
   uint8_t bootIndex;
   uint8_t bootMode;
   bool updateConfig = false;
   rom_header esp_rom_header;
   int i;

   ets_memset(&_bss_start, 0, (&_bss_end - &_bss_start) * sizeof(_bss_start));

#ifdef BOOT_BAUDRATE
   // soft reset doesn't reset PLL/divider, so leave as onfigured
   if (get_reset_reason() != REASON_SOFT_RESTART)
      uart_div_modify(0, UART_CLK_FREQ / BOOT_BAUDRATE);
#endif

#if defined BOOT_DELAY_MICROS && BOOT_DELAY_MICROS > 0
   // delay to slow boot (help see messages when debugging)
   ets_delay_us(BOOT_DELAY_MICROS);
#endif

   ets_printf("\n\nzboot v%u.%u.%u\n", ZBOOT_VERSION_MAJOR, ZBOOT_VERSION_MINOR,
      ZBOOT_VERSION_INCREMENTAL);
   esprom_get_flash_info(&flashSize, &esp_rom_header);

   // Read the zboot config from flash
   SPIRead(BOOT_CONFIG_SECTOR * SECTOR_SIZE, &config, sizeof(config));
   if(config.magic != ZBOOT_CONFIG_MAGIC)
   {
      ets_printf("Invalid zboot config magic\n");
      updateConfig = true;
   }
   else
   {
      uint8_t chksum = zboot_config_checksum(&config);
      if(chksum != config.chksum)
      {
         ets_printf("zboot config checksum mismatch (%02x calculated, %02x expected)\n",
            chksum, config.chksum);
         updateConfig = true;
      }
   }
   if(updateConfig)
   {
      ets_printf("Writing default boot config.\n");
      default_config(&config, flashSize);
      SPIEraseSector(BOOT_CONFIG_SECTOR);
      SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, &config, sizeof(config));
   }

   calculate_frst_index(&bootIndex, &bootMode);

   // Loop through all ROMs, strting with the selected one
   for(runAddr = 0, i = 0; runAddr == 0 && i < config.count; ++i)
   {
      uint8_t tryIndex = bootIndex + i;
      uint32_t tryAddress; 

      if(tryIndex >= config.count)
         tryIndex = 0;
      tryAddress = config.roms[tryIndex];
      DBG("Checking image %u @ %08x\n", tryIndex, tryAddress); 

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
      return;
   }

   if(config.options & ZBOOT_OPTION_GPIO_ERASES_SDKCONFIG)
   {
      uint8_t sec;
      ets_printf("Erasing SDK config sectors before booting.\r\n");
      for (sec = 1; sec < 5; sec++)
      {
         SPIEraseSector((flashSize / SECTOR_SIZE) - sec);
      }
   }

   // re-write config, if required
   if((config.options & ZBOOT_OPTION_UPDATE_BOOT_INDEX) &&
      (config.mode != ZBOOT_MODE_TEMP_ROM) &&
      (bootIndex != config.current_rom))
   {
      ets_printf("Updating zboot config with ROM index %u\n", bootIndex);
      config.current_rom = bootIndex;
      config.chksum = zboot_config_checksum(&config);
      SPIEraseSector(BOOT_CONFIG_SECTOR);
      SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
   }

   flashSize = config.roms[bootIndex];

   // set rtc boot data for app to read
   rtc.magic = ZBOOT_RTC_MAGIC;
   rtc.next_mode = ZBOOT_MODE_STANDARD;
   rtc.last_mode = bootMode; 
   rtc.last_rom = bootIndex; 
   rtc.rom_addr = flashSize;
   rtc.next_rom = 0;
   rtc.spi_mode = esp_rom_header.flags1;
   rtc.spi_speed = esp_rom_header.flags2 & 0xf;
   rtc.spi_size = (esp_rom_header.flags2 >> 4) & 0xf;
   rtc.chksum = zboot_rtc_checksum(&rtc);
   rtc_copy_mem(ZBOOT_RTC_ADDR, &rtc, sizeof(zboot_rtc_data), true);

   ets_printf("Booting ROM %d @ 0x%08x, entry 0x%08x\r\n", bootIndex, flashSize, runAddr);

   // Load the application from a separate function. This function is strategically located
   //  in a section of IRAM designaed for ROM cache so the application's IRAM section
   //  won't overwrite this portion of the bootloader.
   load_rom(flashSize);
}
