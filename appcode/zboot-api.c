/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 * OTA code based on SDK sample from Espressif.
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "zboot-api.h"
#include "esprtc.h"
#include "esprom.h"
#include "zboot_util.h"
#include "zboot.h"

#define DEBUG(...)

extern uint32_t SPIRead(uint32_t, void*, uint32_t);
extern void ets_printf(const char*, ...);
extern void Cache_Read_Enable(uint32_t, uint32_t, uint32_t);

/* ----------------------------------------------------------------------------------------
 * Espressif SDK definitions.
 */

typedef enum {
    SPI_FLASH_RESULT_OK,
    SPI_FLASH_RESULT_ERR,
    SPI_FLASH_RESULT_TIMEOUT
} SpiFlashOpResult;

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec);
SpiFlashOpResult spi_flash_write(uint32_t des_addr, uint32_t *src_addr, uint32_t size);
SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t *des_addr, uint32_t size);

bool system_rtc_mem_read(uint8_t src_addr, void *des_addr, uint16_t load_size);
bool system_rtc_mem_write(uint8_t des_addr, const void *src_addr, uint16_t save_size);

/* TODO: Fix these */
#define os_free(s)   vPortFree(s)
#define os_malloc(s) pvPortMalloc(s)

/* ----------------------------------------------------------------------------------------
 */

static bool zboot_get_config(zboot_config *config)
{
   uint8_t checksum;

   // Read the structure from flash
   if(spi_flash_read(BOOT_CONFIG_SECTOR * SECTOR_SIZE,
      (uint32_t*)config, sizeof(*config)) != SPI_FLASH_RESULT_OK)
   {
      DEBUG("zboot: Failed to read zboot config from flash\n");
      return false;
   }

   // Validate checksum
   checksum = esp_checksum8((uint8_t *) config, sizeof(*config));
   if(checksum != config->chksum)
   {
      DEBUG("zboot: Checksum mismatch on zboot config (calculated %08x, expected %08x)\n",
         checksum, config->chksum);
      return false;
   }

   return true;
}

// Note: This preserves the contents of the sector unused by zboot config 
static bool zboot_set_config(zboot_config *config)
{
   uint8_t *buffer;
   bool success = true;

   buffer = (uint8_t*)os_malloc(SECTOR_SIZE);
   if (NULL == buffer)
   {
      DEBUG("zboot: Failed to allocate memory while writing zboot config\n"); 
      return false;
   }
	
   if(spi_flash_read(BOOT_CONFIG_SECTOR * SECTOR_SIZE,
      (uint32_t*)((void*)buffer), SECTOR_SIZE) != SPI_FLASH_RESULT_OK)
   {
      DEBUG("zboot: Failed to read zboot config sector\n");
      success = false;
   }
   else if(spi_flash_erase_sector(BOOT_CONFIG_SECTOR) != SPI_FLASH_RESULT_OK)
   {
      DEBUG("zboot: Failed to erase zboot config sector\n");
      success = false;
   }
   else
   {
      config->chksum = esp_checksum8((uint8_t*)config, sizeof(*config) - sizeof(uint8_t)); 
      memcpy(buffer, config, sizeof(*config));  // Always at the start of the sector

      if(spi_flash_write(BOOT_CONFIG_SECTOR * SECTOR_SIZE,
         (uint32_t*)((void*)buffer), SECTOR_SIZE) != SPI_FLASH_RESULT_OK)
      {
         DEBUG("zboot: Failed to write zboot config sector\n");
         success = false;
      }
   }
	
   os_free(buffer);
   return success;
}

static bool zboot_get_rtc_data(zboot_rtc_data *rtc)
{
   uint8_t checksum;

   if(!system_rtc_mem_read(ZBOOT_RTC_ADDR, rtc, sizeof(*rtc)))
   {
      DEBUG("zboot: Failed to read RTC memory\n");
      return false;
   }
   checksum = esp_checksum8((uint8_t*)rtc, (sizeof(*rtc)-sizeof(uint8_t)));
   return (rtc->chksum == checksum); 
}

static bool zboot_set_rtc_data(zboot_rtc_data *rtc)
{
   rtc->chksum = esp_checksum8((uint8_t*)rtc, (sizeof(*rtc)-sizeof(uint8_t)));
   return system_rtc_mem_write(ZBOOT_RTC_ADDR, rtc, sizeof(*rtc));
}

static bool zboot_get_image_header(uint32_t offset, zimage_header *header)
{
   if(spi_flash_read(offset, (uint32_t*)header, sizeof(*header)) != SPI_FLASH_RESULT_OK)
      return false;
   else
      return true;
}

// ----------------------------------------------------------------------------------
// Application information 

bool zboot_get_coldboot_index(uint8_t *index)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(NULL != index)
      *index = config.current_rom;
   return true;
}

bool zboot_set_coldboot_index(uint8_t index)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if (index >= config.count)
      return false;
   config.current_rom = index;
   return zboot_set_config(&config);
}

bool zboot_set_gpio_number(uint8_t index)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   config.gpio_num = index;
   return zboot_set_config(&config);
}

bool zboot_set_temp_index(uint8_t index)
{
   zboot_rtc_data rtc;
   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Invalid RTC data; reinitializing\n");
      rtc.magic = ZBOOT_RTC_MAGIC;
      rtc.last_mode = MODE_STANDARD;
      rtc.last_rom = 0;
   }
   rtc.next_mode = MODE_TEMP_ROM;
   rtc.next_rom = index;
   return zboot_set_rtc_data(&rtc);
}

bool zboot_get_current_boot_index(uint8_t *index)
{
   zboot_rtc_data rtc;
   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Failed to read RTC data\n");
      return false;
   }
   if(NULL != index)
      *index = rtc.last_rom;
   return true;
}

bool zboot_get_current_boot_mode(uint8_t *mode)
{
   zboot_rtc_data rtc;
   if (!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Failed to read RTC data\n");
      return false;
   }

   if(NULL != mode)
      *mode = rtc.last_mode;
   return true;
}

bool zboot_get_current_image_info(uint32_t *version, uint32_t *date,
   char *description, uint8_t maxDescriptionLength)
{
   zboot_config config;
   zimage_header header;
   uint8_t bootIndex;
   uint32_t imageAddress;

   if(!zboot_get_current_boot_index(&bootIndex)
   || !zboot_get_config(&config))
      return false;

   if(bootIndex >= config.count)
      return false;
   imageAddress = config.roms[bootIndex];
   if(!zboot_get_image_header(imageAddress, &header))
      return false;

   if(NULL != version)
      *version = header.version;
   if(NULL != date)
      *date = header.date;
   if(NULL != description && maxDescriptionLength > 0)
      strncpy(description, header.description, maxDescriptionLength);

   return true;
}

// ----------------------------------------------------------------------------------
// Write application image

typedef struct
{ 
   bool active;
   uint32_t start_addr;
   uint32_t start_sector;
   int32_t last_sector;
   int32_t last_sector_erased;
   uint8_t extra_count;
   uint8_t extra_bytes[4];
} zboot_write_status;
static zboot_write_status g_zboot_write_status = {0};

// Note: there can be only one write operation in progress at a time
void *zboot_write_init(uint32_t start_addr)
{
   zboot_write_status *status = &g_zboot_write_status;

   if(status->active)
   {
      DEBUG("zboot: Write operation already in progress\n");
      return NULL;
   }

   memset(status, 0, sizeof(*status));
   status->active = true;
   status->start_addr = start_addr;
   status->start_sector = start_addr / SECTOR_SIZE;
   status->last_sector = -1;  // TODO: calculate last sector
   status->last_sector_erased = status->start_sector - 1;
   return (void *) status; 
}

bool zboot_write_end(void *context)
{
   zboot_write_status *status = (zboot_write_status *) context;

   if(!status->active)
   {
      DEBUG("zboot: No write operation in progress\n");
      return false;
   }
 
   // Ensure any remaning bytes get written (needed for files not a multiple of 4 bytes)
   if(status->extra_count != 0)
   {
      for (uint8_t i = status->extra_count; i < 4; ++i)
         status->extra_bytes[i] = 0xff;
      return zboot_write_flash(status, status->extra_bytes, 4);
   }
   return true;
}

// function to do the actual writing to flash
// call repeatedly with more data (max len per write is the flash sector size (4k))
bool zboot_write_flash(void *context, uint8_t *data, uint16_t len)
{
   zboot_write_status *status = (zboot_write_status *) context;
   bool success = true;
   uint8_t *buffer;
   int32_t lastsect;
	
   if (data == NULL || len == 0)
      return true;  // Assume we're done
	
   // get a buffer
   buffer = (uint8_t *)os_malloc(len + status->extra_count);
   if (NULL == buffer)
   {
      DEBUG("zboot: Failed to allocate RAM for flash write\n");
      return false;
   }

   // Copy any remaining bytes from last chunk, then copy new data
   if (status->extra_count > 0)
      memcpy(buffer, status->extra_bytes, status->extra_count);
   memcpy(buffer + status->extra_count, data, len);

   // Write length must be multiple of 4; save any remaining bytes for next chunk 
   len += status->extra_count;
   status->extra_count = len % 4;
   len -= status->extra_count;
   memcpy(status->extra_bytes, buffer + len, status->extra_count);

   // Ensure new chunk will fit 
   if ((status->last_sector >= 0) && 
       (status->start_addr + len) > (status->last_sector * SECTOR_SIZE))
   {
      DEBUG("zboot: Flash overrun\n");
      success = false;
   }
   else
   {
      // erase any additional sectors needed by this chunk
      lastsect = ((status->start_addr + len) - 1) / SECTOR_SIZE;
      while (lastsect > status->last_sector_erased)
      {
         ++(status->last_sector_erased);
         spi_flash_erase_sector(status->last_sector_erased);
      }

      // write current chunk
      //os_printf("write addr: 0x%08x, len: 0x%04x\r\n", status->start_addr, len);
      if (spi_flash_write(status->start_addr, (uint32_t *)((void*)buffer), len) != SPI_FLASH_RESULT_OK)
      {
         DEBUG("zboot: Flash write failed\n");
         success = false;
      }
      else
         status->start_addr += len;
   }

   os_free(buffer);
   return success;
}

// ----------------------------------------------------------------------------------

static uint8_t rBoot_mmap_1 = 0xff;
static uint8_t rBoot_mmap_2 = 0xff;

// This function must exist in IRAM 
void __attribute__((section(".iram.text"))) Cache_Read_Enable_New(void)
{
   if (rBoot_mmap_1 == 0xff)
   {
      uint32_t val;
      zboot_config conf;

      SPIRead(BOOT_CONFIG_SECTOR * SECTOR_SIZE, &conf, sizeof(conf));

      // rtc support here isn't written ideally, we don't read the whole structure and
      // we don't check the checksum. However this code is only run on first boot, so
      // the rtc data should have just been set and the user app won't have had chance
      // to corrupt it or suspend or anything else that might upset it. And if
      // it were bad what should we do anyway? We can't just ignore bad data here, we
      // need it. But the main reason is that this code must be in iram, which is in
      // very short supply, doing this "properly" increases the size about 3x

      // used only to calculate offset into structure, should get optimized out
      zboot_rtc_data rtc;
      uint8_t off = (uint8_t*)&rtc.last_rom - (uint8_t*)&rtc;
      // get the four bytes containing the one of interest
      volatile uint32_t *rtcd = (uint32_t*)(0x60001100 + ZBOOT_RTC_ADDR + (off & ~3));
      val = *rtcd;
      // extract the one of interest
      val = ((uint8_t*)&val)[off & 3];
      // get address of rom
      val = conf.roms[val];

      val /= 0x100000;

      rBoot_mmap_2 = val / 2;
      rBoot_mmap_1 = val % 2;
		
      //ets_printf("mmap %d,%d,1\r\n", rBoot_mmap_1, rBoot_mmap_2);
   }
	
   Cache_Read_Enable(rBoot_mmap_1, rBoot_mmap_2, 1);
}
