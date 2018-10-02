/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#ifndef ZBOOT_H
#define ZBOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "appcode/zboot-api.h"

#define ZBOOT_VERSION_MAJOR        1
#define ZBOOT_VERSION_MINOR        0
#define ZBOOT_VERSION_INCREMENTAL  0

// uncomment to use only c code
// if you aren't using gcc you may need to do this
//#define BOOT_NO_ASM

// uncomment to enable GPIO booting of specific rom
// (specified in rBoot config block)
// cannot be used at same time as BOOT_GPIO_SKIP_ENABLED
//#define BOOT_GPIO_ENABLED

// uncomment to enable GPIO rom skip mode, trigger
// GPIO at boot time to skip to next rom
// cannot be used at same time as BOOT_GPIO_ENABLED
//#define BOOT_GPIO_SKIP_ENABLED

// set the GPIO pin used by GPIO modes above (will default
// to 16 if not manually set), only applicable when
// BOOT_GPIO_ENABLED or BOOT_GPIO_SKIP_ENABLED is enabled
//#define BOOT_GPIO_NUM 16

// uncomment to add a boot delay, allows you time to connect
// a terminal before rBoot starts to run and output messages
// value is in microseconds
//#define BOOT_DELAY_MICROS 2000000

#define BOOT_CONFIG_SECTOR 2

// defaults for unset user options
#ifndef BOOT_GPIO_NUM
#define BOOT_GPIO_NUM 16
#endif

#ifndef MAX_ROMS
#define MAX_ROMS 4
#endif

// --------------------------------------------------------------------------------------------

#pragma pack(push,1)
typedef struct {
   uint32_t magic;
      #define ZBOOT_CONFIG_MAGIC 0xdcce4b28 
   uint8_t mode;            /* one of ZBOOT_MODE_* */
   uint8_t current_rom;     ///< Currently selected ROM (will be used for next standard boot)
   uint8_t gpio_rom;        ///< ROM to use for GPIO boot (hardware switch) with mode set to MODE_GPIO_ROM
   uint8_t count;           ///< Quantity of ROMs available to boot
   uint32_t roms[MAX_ROMS]; ///< Flash addresses of each ROM
   uint8_t failsafe_rom;
   uint8_t options;         /* one of ZBOOT_OPTION_* */
   uint8_t gpio_num;
   uint8_t chksum;          ///< Checksum of this configuration structure
} zboot_config;
#pragma pack(pop)

// --------------------------------------------------------------------------------------------

#define ZBOOT_RTC_ADDR 64  // Start of RTC "user" area

#pragma pack(push,1)
typedef struct {
   uint32_t magic;
      #define ZBOOT_RTC_MAGIC 0x2334ae68
   uint8_t next_mode;        ///< The next boot mode, defaults to MODE_STANDARD - can be set to MODE_TEMP_ROM
   uint8_t last_mode;        ///< The last (this) boot mode - can be MODE_STANDARD, MODE_GPIO_ROM or MODE_TEMP_ROM
   uint8_t last_rom;         ///< The last (this) boot rom number
   uint8_t next_rom;         ///< The next boot rom number when next_mode set to MODE_TEMP_ROM
   uint32_t rom_addr;
   uint8_t spi_speed;
   uint8_t spi_size;
   uint8_t spi_mode;
   uint8_t chksum;
} zboot_rtc_data;
#pragma pack(pop)

// --------------------------------------------------------------------------------------------

#define ZIMAGE_HEADER_OFFSET_MAGIC   0
#define ZIMAGE_HEADER_OFFSET_COUNT   1
#define ZIMAGE_HEADER_OFFSET_ENTRY   2
#define ZIMAGE_HEADER_OFFSET_VERSION 3
#define ZIMAGE_HEADER_OFFSET_DATE    4

#pragma pack(push,0)
typedef struct
{
   uint32_t magic;
      #define ZIMAGE_MAGIC 0x279bfbf1
   uint32_t count;     // section count
   uint32_t entry;     // entrypoint address
   uint32_t version;
   uint32_t date;
   uint32_t reserved[3];
   char     description[88];
} zimage_header;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
