/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#ifndef ZBOOT_API_H
#define ZBOOT_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool zboot_get_coldboot_index(uint8_t *index);
bool zboot_set_coldboot_index(uint8_t index);
bool zboot_set_temp_index(uint8_t index);
bool zboot_set_gpio_number(uint8_t index);
bool zboot_get_current_boot_index(uint8_t *index);
bool zboot_get_current_boot_mode(uint8_t *mode);
bool zboot_get_current_image_info(uint32_t *version, uint32_t *date,
   char *description, uint8_t maxDescriptionLength);

void *zboot_write_init(uint32_t start_addr);
bool zboot_write_end(void *context);
bool zboot_write_flash(void *context, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZBOOT_API_H */
