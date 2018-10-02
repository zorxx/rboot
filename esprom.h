/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#ifndef ESPROM_H
#define ESPROM_H

#include <stdint.h>
#include <stdbool.h>

#define SECTOR_SIZE 0x1000 // flash sector size

// Standard ESP8266 ROM header
#pragma pack(push,0)
typedef struct
{
   uint8_t magic;
   uint8_t count;
   uint8_t flags1;
   uint8_t flags2;
   uint32_t entry;
} rom_header;
#pragma pack(pop)

#define ESP_CHKSUM_INIT 0xef
static uint8_t esp_checksum8(uint8_t *start, uint8_t length)
{
   uint8_t chksum = ESP_CHKSUM_INIT;
   while(length > 0)
   {
      chksum ^= *start;
      ++start;
      --length;
   }
   return chksum;
}

bool esprom_get_flash_info(uint32_t *size, rom_header *header);

#endif /* ESPROM_H */
