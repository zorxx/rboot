/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#ifndef ZBOOT_UTIL_H
#define ZBOOT_UTIL_H

#include <stdint.h>
#include "zboot.h"

uint8_t zboot_checksum8(uint8_t *start, uint8_t length);

void default_config(zboot_config *config, uint32_t flashsize);
#define zboot_config_checksum(config) \
      zboot_checksum8((uint8_t*)(config), sizeof(zboot_config)-sizeof(uint8_t))

#define zboot_rtc_checksum(rtc) \
      zboot_checksum8((uint8_t*)(rtc), sizeof(zboot_rtc_data)-sizeof(uint8_t))

#endif /* ZBOOT_UTIL_H */
