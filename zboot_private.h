/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#ifndef ZBOOT_PRIVATE_H
#define ZBOOT_PRIVATE_H

#include "zboot.h"

#define ROM_MAGIC      0xe9
#define ROM_MAGIC_NEW1 0xea
#define ROM_MAGIC_NEW2 0x04

#define BUFFER_SIZE 0x1000

// esp8266 built in ROM functions
extern uint32_t SPIRead(uint32_t addr, void *outptr, uint32_t len);
extern uint32_t SPIEraseSector(int);
extern uint32_t SPIWrite(uint32_t addr, void *inptr, uint32_t len);
extern void ets_printf(char*, ...);
extern void ets_delay_us(int);
extern void ets_memset(void*, uint8_t, uint32_t);
extern void ets_memcpy(void*, const void*, uint32_t);

// functions we'll call by address
typedef void stage2a(uint32_t);
typedef void usercode(void);

typedef struct
{ 
   uint32_t address;
   uint32_t length;
} section_header;

#endif /* ZBOOT_PRIVATE_H */
