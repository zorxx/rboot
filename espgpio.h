/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#ifndef ESPGPIO_H
#define ESPGPIO_H

#include <stdint.h>
#include <stdbool.h>

bool gpio_asserted(uint8_t index);

#endif /* ESPGPIO_H */
