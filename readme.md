## zboot
An open source boot loader for the ESP8266

based on rBoot, by Richard A Burton, richardaburton@gmail.com

## Overview

This another bootloader, originally based off rboot, but has the following notable features:
- Works with applications created from latest ESP8266 SDK, which have an entrypoint in SPI flash. Applications with IRAM entrypoints (e.g. Arduino) still work too.
- Includes build date, version information and description in the application header, so you can can see the details of the applications stored in flash. This is particularly helpful when managing applications and selecting the boot index.
- Uses 0 bytes of stack space. Before starting the application, the stack is reset so there's no wasted DRAM.
- Similar to rboot, zboot allows the use of the entire flash part for OTA updates and multiple applications stored in flash.

## Details

zboot executes from the upper-most 16 kB of IRAM. This area is normally reserved for SPI flash cache, to allow for execution of ROM code, but since zboot executes with cache disabled, this area may be used. Since zboot doesn't occupy other areas of IRAM, the application may make full use of IRAM.

Immediately prior to execution of the application, zboot calls the ROM function to enable SPI flash cache. This allows support for executing applications with an entrypoint in SPI flash. zboot maps the proper 1MB of SPI flash for the selected application, then begins execution. This procedure is made possible by abusing the return address of the `Cache_Read_Enable` ROM function; the `Cache_Read_Enable` function is unwittingly responsible for calling the application's entrypoint.
