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

#define ZBOOT_MODE_STANDARD    0x00
#define ZBOOT_MODE_GPIO_ROM    0x01
#define ZBOOT_MODE_TEMP_ROM    0x02
#define ZBOOT_MODE_GPIO_SKIP   0x03
#define ZBOOT_MODE_FAILSAFE    0x04

#define ZBOOT_OPTION_GPIO_ERASES_SDKCONFIG 0x01
#define ZBOOT_OPTION_UPDATE_BOOT_INDEX     0x02

#ifdef __cplusplus
extern "C" {
#endif

bool zboot_set_coldboot_index(uint8_t index);
bool zboot_set_temp_index(uint8_t index);
bool zboot_set_failsafe_index(uint8_t index);
bool zboot_set_boot_mode(uint8_t mode);
bool zboot_set_option(uint8_t option, bool enable);
bool zboot_set_gpio_number(uint8_t index);
bool zboot_erase_config(void);
bool zboot_invalidate_index(uint8_t index);

bool zboot_get_image_address(uint8_t index, uint32_t *address);
bool zboot_get_coldboot_index(uint8_t *index);
bool zboot_get_failsafe_index(uint8_t *index);
bool zboot_get_temp_index(uint8_t *index);
bool zboot_get_current_boot_index(uint8_t *index);
bool zboot_get_current_boot_mode(uint8_t *mode);
bool zboot_get_boot_mode(uint8_t *mode);
bool zboot_get_options(uint8_t *options);
bool zboot_get_current_image_info(uint32_t *version, uint32_t *date,
  uint32_t *address, uint8_t *index, char *description, uint8_t maxDescriptionLength);
bool zboot_find_best_write_index(uint8_t *index, bool overwriteOldest);
bool zboot_get_image_count(uint8_t *count);
bool zboot_get_image_info(uint8_t index, uint32_t *version, uint32_t *date,
   uint32_t *address, char *description, uint8_t maxDescriptionLength);

void *zboot_write_init(uint32_t start_addr);
bool zboot_write_end(void *context);
bool zboot_write_flash(void *context, uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZBOOT_API_H */
