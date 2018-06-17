/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#include "zboot_private.h"

usercode* NOINLINE load_rom(uint32_t readpos)
{
   uint8_t sectcount;
   uint8_t *writepos;
   uint32_t remaining;
   uint32_t header_count;	
   uint32_t header_entry;	
   section_header section;
	
   // read rom header
   readpos += sizeof(uint32_t); // skip magic
   SPIRead(readpos, &header_count, sizeof(header_count));
   readpos += sizeof(uint32_t);
   SPIRead(readpos, &header_entry, sizeof(header_entry));
   readpos += 28 * sizeof(uint32_t); 

   // copy all the sections
   for (sectcount = header_count; sectcount > 0; sectcount--)
   {
      // read section header
      SPIRead(readpos, &section, sizeof(section_header));
      readpos += sizeof(section_header);

      // get section address and length
      writepos = (uint8_t *)section.address;
      remaining = section.length;

      if(0 == writepos)
      {
         readpos += remaining;
         continue;
      }	
		
      while (remaining > 0)
      {
         uint32_t readlen = (remaining < READ_SIZE) ? remaining : READ_SIZE;
         SPIRead(readpos, writepos, readlen);
         readpos += readlen;
         writepos += readlen;
         remaining -= readlen;
      }	
   }

   return (usercode *) header_entry; 
}

#ifdef BOOT_NO_ASM

void call_user_start(uint32_t readpos)
{
   usercode* user;
   user = load_rom(readpos);
   user();
}

#else

void call_user_start(uint32_t readpos)
{
   __asm volatile ( 
      "mov a15, a0\n"     // store return addr, we already splatted a15!
      "call0 load_rom\n"  // load the rom
      "mov a0, a15\n"     // restore return addr
      "jx a2\n"           // now jump to the rom code
   );
}

#endif
