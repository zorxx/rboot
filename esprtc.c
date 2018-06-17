/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#include <stdlib.h>
#include "esprtc.h"

// --------------------------------------------------------------------------------------------------
// RTC operations 

bool rtc_copy_mem(uint32_t offset, void *buffer, uint32_t length, bool isWrite) 
{
   volatile uint32_t *ram;
   volatile uint32_t *rtc;
   uint32_t blocks;

   if(NULL == buffer || (((uint32_t)buffer) & 0x3) != 0) // Buffer must be 4-byte aligned
      return false;
   if(length == 0 || (length & 0x3) != 0) // Length must be 4-byte multiple
      return false;
   if (offset < 64) // Make sure this is a user block
      return false;
   if (length > (ESP_RTC_MEM_SIZE - offset)) // Ensure all data fits
      return false;

   length >>= 2; // Convert to 32-bit words
   offset >>= 2;

   // copy the data
   ram = (volatile uint32_t* )buffer;
   rtc = (volatile uint32_t *)ESP_RTC_MEM_START + offset;
   for (blocks = 0; blocks < length; ++blocks, ++ram, ++rtc)
   {
      if(isWrite)
         *rtc = *ram;
      else
         *ram = *rtc;
    }

    return true;
}

