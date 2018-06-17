/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#ifndef ESPRTC_H
#define ESPRTC_H

#include <stdint.h>
#include <stdbool.h>

enum rst_reason
{
   REASON_DEFAULT_RST      = 0,
   REASON_WDT_RST          = 1,
   REASON_EXCEPTION_RST    = 2,
   REASON_SOFT_WDT_RST     = 3,
   REASON_SOFT_RESTART     = 4,
   REASON_DEEP_SLEEP_AWAKE = 5,
   REASON_EXT_SYS_RST      = 6
};

#define ESP_RTC_MEM_START ((volatile uint32_t*) 0x60001100)
#define ESP_RTC_MEM_SIZE  0x300

// Reset reason stored in the first 32-bits of RTC memory
#define get_reset_reason() \
   ((enum rst_reason)(ESP_RTC_MEM_START[0]))

bool rtc_copy_mem(uint32_t offset, void *buffer, uint32_t length, bool isWrite); 

#endif /* ESPRTC_H */
