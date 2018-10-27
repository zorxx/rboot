/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#include "esprom.h"
#include "zboot_private.h"
#include "zboot_util.h"

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

void default_config(zboot_config *config, uint32_t flashsize)
{
   ets_memset(config, 0, sizeof(*config));
   config->magic = ZBOOT_CONFIG_MAGIC;
   config->count = BOOT_DEFAULT_CONFIG_IMAGE_COUNT;
   config->roms[0] = BOOT_DEFAULT_CONFIG_ROM0;
   config->roms[1] = BOOT_DEFAULT_CONFIG_ROM1;
   config->roms[2] = BOOT_DEFAULT_CONFIG_ROM2;
   config->roms[3] = BOOT_DEFAULT_CONFIG_ROM3;
#ifdef BOOT_GPIO_ENABLED
   config->mode = ZBOOT_MODE_GPIO_ROM;
#endif
#ifdef BOOT_GPIO_SKIP_ENABLED
   config->mode = ZBOOT_MODE_GPIO_SKIP;
#endif
   config->chksum = zboot_config_checksum(config);
}

