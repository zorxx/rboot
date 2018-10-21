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

#if defined(ZBOOT_API_DEBUG)
#define DEBUG(...) ets_printf(__VA_ARGS__)
#else
#define DEBUG(...)
#endif

#if defined(ARDUINO_ARCH_ESP8266)

extern void vPortFree(void *pv);
void *pvPortMalloc(size_t xSize);
#define os_free(s) vPortFree(s)
#define os_malloc(s) pvPortMalloc(s)

#else

#include <malloc.h>
#define os_free(s) free(s)
#define os_malloc(s) malloc(s)

#endif

static zboot_rtc_data g_zboot_rtc;
static bool g_zboot_rtc_set = false;

extern void ets_printf(const char*, ...);
extern void Cache_Read_Enable_original(uint8_t, uint8_t, uint8_t);

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

bool system_rtc_mem_write(uint8_t des_addr, const void *src_addr, uint16_t save_size);
bool system_rtc_mem_read(uint8_t des_addr, const void *src_addr, uint16_t save_size);

/* ----------------------------------------------------------------------------------------
 * Private Helper Functions
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

   if(config->magic != ZBOOT_CONFIG_MAGIC)
      return false;

   // Validate checksum
   checksum = zboot_config_checksum(config);
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
      if(NULL != config)
      {
         config->chksum = zboot_config_checksum(config);
         memcpy(buffer, config, sizeof(*config));  // Always at the start of the sector
      }
      else
         memset(buffer, 0, sizeof(*config));  // Erase configuration

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

   if(g_zboot_rtc_set)
   {
      memcpy(rtc, &g_zboot_rtc, sizeof(*rtc));
      return true;
   }

   if(!system_rtc_mem_read(ZBOOT_RTC_ADDR/sizeof(uint32_t), rtc, sizeof(*rtc)))
   {
      DEBUG("%s: Failed to read RTC memory\n", __func__);
      return false;
   }
   checksum = zboot_rtc_checksum(rtc);
   if(rtc->chksum != checksum)
   {
      DEBUG("%s: checksum mismatch calculated=%02x, expected=%02x\n",
         __func__, checksum, rtc->chksum);
      return false;
   }

   memcpy(&g_zboot_rtc, rtc, sizeof(*rtc));
   g_zboot_rtc_set = true;
   return true;
}

static bool zboot_set_rtc_data(zboot_rtc_data *rtc)
{
   rtc->chksum = esp_checksum8((uint8_t*)rtc, (sizeof(*rtc)-sizeof(uint8_t)));
   return system_rtc_mem_write(ZBOOT_RTC_ADDR/sizeof(uint32_t), rtc, sizeof(*rtc));
}

static bool zboot_get_image_header(uint32_t offset, zimage_header *header)
{
   if(spi_flash_read(offset, (uint32_t*)header, sizeof(*header)) != SPI_FLASH_RESULT_OK)
      return false;
   else
      return true;
}

// ----------------------------------------------------------------------------------
// Set Operations

bool zboot_set_coldboot_index(uint8_t index)
{
   zboot_config config;
   DEBUG("%s: index\n", __func__, index);
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

bool zboot_invalidate_index(uint8_t index)
{
   zboot_config config;
   int32_t sector;
   DEBUG("%s: index\n", __func__, index);
   if(!zboot_get_config(&config))
      return false;
   if(index >= config.count)
      return false;
   sector = config.roms[index] / SECTOR_SIZE;
   return (spi_flash_erase_sector(sector) == SPI_FLASH_RESULT_OK);
}

bool zboot_set_boot_mode(uint8_t mode)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   config.mode = mode;
   return zboot_set_config(&config);
}

bool zboot_set_option(uint8_t option, bool enable)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(enable)
      config.options |= option;
   else
      config.options &= ~option;
   return zboot_set_config(&config);
}

bool zboot_set_failsafe_index(uint8_t index)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(index >= config.count)
      return false;
   config.failsafe_rom = index;
   return zboot_set_config(&config);
}

bool zboot_set_temp_index(uint8_t index)
{
   zboot_rtc_data rtc;
   zboot_config config;

   if(!zboot_get_config(&config))
      return false;
   if(index >= config.count)
      return false;

   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Invalid RTC data; reinitializing\n");
      rtc.magic = ZBOOT_RTC_MAGIC;
      rtc.last_mode = ZBOOT_MODE_STANDARD;
      rtc.last_rom = 0;
   }
   rtc.next_mode = ZBOOT_MODE_TEMP_ROM;
   rtc.next_rom = index;
   return zboot_set_rtc_data(&rtc);
}

bool zboot_erase_config(void)
{
   return zboot_set_config(NULL);
}

// ----------------------------------------------------------------------------------
// Get Operations

bool zboot_get_coldboot_index(uint8_t *index)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(NULL != index)
      *index = config.current_rom;
   return true;
}

bool zboot_get_failsafe_index(uint8_t *index)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(config.failsafe_rom >= config.count)
      return false;
   if(NULL != index)
      *index = config.failsafe_rom;
   return true;
}

bool zboot_get_temp_index(uint8_t *index)
{
   zboot_rtc_data rtc;
   if(!zboot_get_rtc_data(&rtc))
      return false;
   if(rtc.next_mode != ZBOOT_MODE_TEMP_ROM)
      return false;
   if(NULL != index)
      *index = rtc.next_rom;
   return true;
}

bool zboot_get_boot_mode(uint8_t *mode)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(NULL != mode)
      *mode = config.mode;
   return true;
}

bool zboot_get_options(uint8_t *options)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(NULL != options)
      *options = config.options;
   return true;
}

bool zboot_get_image_address(uint8_t index, uint32_t *address)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(index >= config.count)
      return false;
   if(NULL != address)
      *address = config.roms[index];
   return true;
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
   uint32_t *address, uint8_t *index, char *description, uint8_t maxDescriptionLength)
{
   zimage_header header;
   zboot_rtc_data rtc;

   DEBUG("%s\n", __func__);

   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Failed to get zboot data\n");
      return false;
   }

   DEBUG("zboot: Current boot index %u, address %08x\n", rtc.last_rom, rtc.rom_addr);
   if(!zboot_get_image_header(rtc.rom_addr, &header))
   {
      DEBUG("zboot: Failed to read image header\n");
      return false;
   }

   if(header.magic != ZIMAGE_MAGIC)
      return false;

   if(NULL != version)
      *version = header.version;
   if(NULL != date)
      *date = header.date;
   if(NULL != address)
      *address = rtc.rom_addr;
   if(NULL != index)
      *index = rtc.last_rom;
   if(NULL != description && maxDescriptionLength > 0)
      strncpy(description, header.description, maxDescriptionLength);

   return true;
}

bool zboot_find_best_write_index(uint8_t *index, bool overwriteOldest)
{
   uint8_t idx, best_index = 0;
   uint32_t best_date = 0xffffffff;
   bool found = false;
   zboot_config config;
   zboot_rtc_data rtc;

   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Failed to get zboot data\n");
      return false;
   }

   if(!zboot_get_config(&config))
   {
      DEBUG("zboot: Failed to read zboot config\n");
      return false;
   }

   for(idx = 0; idx < config.count; ++idx)
   {
      uint32_t date;
      if(idx == rtc.last_rom)
      {
         // not possible to overwrite currently-executing image
      }
      else if(zboot_get_image_info(idx, NULL, &date, NULL, NULL, 0))
      {
         if(overwriteOldest && (date < best_date))
         {
            best_index = idx;
            best_date = date;
            found = true;
         }
      }
      else
      {
         found = true;
         best_index = idx;
         break;  // Stop search
      }
   }

   if(!found)
   {
      best_index = (rtc.last_rom + 1) % config.count;
      found = (best_index != rtc.last_rom);
   }

   if(found && index != NULL)
      *index = best_index;
   return found;
}

bool zboot_get_image_count(uint8_t *count)
{
   zboot_config config;
   if(!zboot_get_config(&config))
      return false;
   if(NULL != count)
      *count = config.count;
   return true;
}

bool zboot_get_image_info(uint8_t index, uint32_t *version, uint32_t *date,
   uint32_t *address, char *description, uint8_t maxDescriptionLength)
{
   zboot_config config;
   zimage_header header;

   if(!zboot_get_config(&config))
   {
      DEBUG("zboot: Failed to read zboot config\n");
      return false;
   }

   if(index > config.count)
      return false;

   if(!zboot_get_image_header(config.roms[index], &header))
   {
      DEBUG("zboot: Failed to read header for ROM index %u @ %08x\n", index, config.roms[index]);
      return false;
   }

   if(header.magic != ZIMAGE_MAGIC)
      return false;

   if(NULL != address)
      *address = config.roms[index];
   if(NULL != version)
      *version = header.version;
   if(NULL != date)
      *date = header.date;
   if(NULL != description && maxDescriptionLength > 0)
      strncpy(description, header.description, maxDescriptionLength);

   return true;
}

bool zboot_get_flash_size(uint8_t *size)
{
   zboot_rtc_data rtc;
   DEBUG("%s\n", __func__);

   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Failed to get rtc data\n");
      return false;
   }

   if(NULL != size)
      *size = rtc.spi_size;
   return true;
}

bool zboot_get_flash_speed(uint8_t *speed)
{
   zboot_rtc_data rtc;
   DEBUG("%s\n", __func__);

   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Failed to get rtc data\n");
      return false;
   }

   if(NULL != speed)
      *speed = rtc.spi_speed;
   return true;
}

bool zboot_get_flash_mode(uint8_t *mode)
{
   zboot_rtc_data rtc;
   DEBUG("%s\n", __func__);

   if(!zboot_get_rtc_data(&rtc))
   {
      DEBUG("zboot: Failed to get rtc data\n");
      return false;
   }

   if(NULL != mode)
      *mode = rtc.spi_mode;
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
   bool result = false;

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
      result = zboot_write_flash(status, status->extra_bytes, 4);
   }
   else
      result = true;

   status->active = false;
   return result;
}

// function to do the actual writing to flash
// call repeatedly with more data (max len per write is the flash sector size (4k))
bool zboot_write_flash(void *context, const uint8_t *data, const uint16_t len)
{
   zboot_write_status *status = (zboot_write_status *) context;
   bool success = true;
   uint8_t *buffer;
   int32_t lastsect;
   uint16_t length = len;
	
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
   length += status->extra_count;
   status->extra_count = length % 4;
   length -= status->extra_count;
   memcpy(status->extra_bytes, buffer + length, status->extra_count);

   // Ensure new chunk will fit 
   if ((status->last_sector >= 0) && 
       (status->start_addr + length) > (status->last_sector * SECTOR_SIZE))
   {
      DEBUG("zboot: Flash overrun\n");
      success = false;
   }
   else
   {
      // erase any additional sectors needed by this chunk
      lastsect = ((status->start_addr + length) - 1) / SECTOR_SIZE;
      while (lastsect > status->last_sector_erased)
      {
         ++(status->last_sector_erased);
         spi_flash_erase_sector(status->last_sector_erased);
      }

      // write current chunk
      if (spi_flash_write(status->start_addr, (uint32_t *)((void*)buffer), length) != SPI_FLASH_RESULT_OK)
      {
         DEBUG("zboot: Flash write failed\n");
         success = false;
      }
      else
         status->start_addr += length;
   }

   os_free(buffer);
   return success;
}

// ----------------------------------------------------------------------------------

void zboot_api_init(void)
{
   zboot_rtc_data rtc;

   zboot_get_rtc_data(&rtc);
}

#define CACHE_READ_ENABLE()                                                                 \
   volatile zboot_rtc_data *rtc =                                                           \
      (volatile zboot_rtc_data *)(ESP_RTC_MEM_START + (ZBOOT_RTC_ADDR / sizeof(uint32_t))); \
   Cache_Read_Enable_original((rtc->rom_addr >> 20) & 1, (rtc->rom_addr >> 21) & 1, 1);

void __attribute__((section(".entry.text"))) Cache_Read_Enable_New(void)
{
   CACHE_READ_ENABLE();
}

void __attribute__((section(".entry.text"))) Cache_Read_Enable(uint8_t a, uint8_t b, uint8_t c)
{
   CACHE_READ_ENABLE();
}
