/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#include <stdlib.h>
#include "esprom.h"

bool esprom_get_flash_size(uint32_t *size)
{
   rom_header header;
   uint8_t flag;
   uint32_t flashsize = 0x80000; // assume at least 4mbit

   SPIRead(0, &header, sizeof(header));

   ets_printf("Flash: ");
   flag = header.flags2 >> 4;
   if (flag == 0) {
      ets_printf("4");
      flashsize = 0x80000;
   } else if (flag == 1) {
      ets_printf("2");
      flashsize = 0x40000;
   } else if (flag == 2) {
      ets_printf("8");
      flashsize = 0x100000;
   } else if (flag == 3) {
      ets_printf("16");
      flashsize = 0x200000;
   } else if (flag == 4) {
      ets_printf("32");
      flashsize = 0x400000;
   } else
      ets_printf("unknown");
   ets_printf(" Mbit, ");

   if (header.flags1 == 0) 
      ets_printf("QIO, ");
   else if (header.flags1 == 1)
      ets_printf("QOUT, ");
   else if (header.flags1 == 2)
      ets_printf("DIO, ");
   else if (header.flags1 == 3)
      ets_printf("DOUT, ");
   else
      ets_printf("unknown mode, ");

   flag = header.flags2 & 0x0f;
   if (flag == 0)
      ets_printf("40");
   else if (flag == 1)
      ets_printf("26.7");
   else if (flag == 2)
      ets_printf("20");
   else if (flag == 0x0f)
      ets_printf("80");
   else
      ets_printf("unknown speed");
   ets_printf(" MHz\n");

   if(NULL != size)
      *size = flashsize;
   return true;
}

