/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#include <stdlib.h>
#include "esprom.h"

bool esprom_get_flash_info(uint32_t *size, rom_header *header)
{
   uint8_t spi_size, spi_speed, spi_mode;
   uint32_t flashsize = 0x80000; // assume at least 4mbit

   SPIRead(0, header, sizeof(*header));

   ets_printf("Flash: ");
   spi_size = header->flags2 >> 4;
   if (spi_size == 0) {
      ets_printf("4");
      flashsize = 0x80000;
   } else if (spi_size == 1) {
      ets_printf("2");
      flashsize = 0x40000;
   } else if (spi_size == 2) {
      ets_printf("8");
      flashsize = 0x100000;
   } else if (spi_size == 3) {
      ets_printf("16");
      flashsize = 0x200000;
   } else if (spi_size == 4) {
      ets_printf("32");
      flashsize = 0x400000;
   } else
      ets_printf("unknown");
   ets_printf(" Mbit, ");

   spi_mode = header->flags1;
   if (spi_mode == 0) 
      ets_printf("QIO, ");
   else if (spi_mode == 1)
      ets_printf("QOUT, ");
   else if (spi_mode == 2)
      ets_printf("DIO, ");
   else if (spi_mode == 3)
      ets_printf("DOUT, ");
   else
      ets_printf("unknown mode, ");

   spi_speed = header->flags2 & 0x0f;
   if (spi_speed == 0)
      ets_printf("40");
   else if (spi_speed == 1)
      ets_printf("26.7");
   else if (spi_speed == 2)
      ets_printf("20");
   else if (spi_speed == 0x0f)
      ets_printf("80");
   else
      ets_printf("unknown speed");
   ets_printf(" MHz\n");

   if(NULL != size)
      *size = flashsize;

   return true;
}
