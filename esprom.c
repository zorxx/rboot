/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#include <stdlib.h>
#include "esprom.h"

#define BIT5 (1 << 5)
#define BIT8 (1 << 8)
#define BIT12 (1 << 12)

#define ETS_UNCACHED_ADDR(addr) (addr)

#define READ_PERI_REG(addr)                             (*((volatile uint32_t *)ETS_UNCACHED_ADDR(addr)))
#define WRITE_PERI_REG(addr, val)                       (*((volatile uint32_t *)ETS_UNCACHED_ADDR(addr))) = (uint32_t)(val)
#define CLEAR_PERI_REG_MASK(reg, mask)                  WRITE_PERI_REG((reg), (READ_PERI_REG(reg) & (~(mask))))
#define SET_PERI_REG_MASK(reg, mask)                    WRITE_PERI_REG((reg), (READ_PERI_REG(reg) | (mask)))
#define SET_PERI_REG_BITS(reg, bit_map, value, shift)   (WRITE_PERI_REG((reg), (READ_PERI_REG(reg) & (~((bit_map) << (shift)))) | ((value) << (shift)) ))

#define PERIPHS_SPI_FLASH_USRREG        (0x60000200 + 0x1c)
#define PERIPHS_SPI_FLASH_CTRL          (0x60000200 + 0x08)
#define PERIPHS_IO_MUX_CONF_U           (0x60000800)

#define SPI0_CLK_EQU_SYSCLK             BIT8
#define SPI_FLASH_CLK_EQU_SYSCLK        BIT12

bool esprom_get_flash_info(uint32_t *size, rom_header *header)
{
   uint8_t spi_size, spi_speed, spi_mode;
   uint32_t flashsize = 0x80000; // assume at least 4mbit
   uint32_t freqdiv, freqbits;

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

   SET_PERI_REG_MASK(PERIPHS_SPI_FLASH_USRREG, BIT5);

   if(spi_speed < 3)
      freqdiv = spi_speed + 2;
   else if (0x0F == spi_speed)
      freqdiv = 1;
   else
      freqdiv = 2;

   if(freqdiv <= 1)
   {
      freqbits = SPI_FLASH_CLK_EQU_SYSCLK;
      SET_PERI_REG_MASK(PERIPHS_SPI_FLASH_CTRL, SPI_FLASH_CLK_EQU_SYSCLK);
      SET_PERI_REG_MASK(PERIPHS_IO_MUX_CONF_U, SPI0_CLK_EQU_SYSCLK);
   }
   else
   {
      freqbits = ((freqdiv - 1) << 8) + ((freqdiv / 2 - 1) << 4) + (freqdiv - 1);
      CLEAR_PERI_REG_MASK(PERIPHS_SPI_FLASH_CTRL, SPI_FLASH_CLK_EQU_SYSCLK);
      CLEAR_PERI_REG_MASK(PERIPHS_IO_MUX_CONF_U, SPI0_CLK_EQU_SYSCLK);
   }
   SET_PERI_REG_BITS(PERIPHS_SPI_FLASH_CTRL, 0xfff, freqbits, 0);

   return true;
}
