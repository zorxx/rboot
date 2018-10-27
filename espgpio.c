/* \brief zboot - bootloader for ESP8266 
 * Copyright 2018 Zorxx Software, zorxx@zorxx.com
 * Copyright 2015 Richard A Burton, richardaburton@gmail.com
 * See license.txt for license terms.
 * Based on rBoot from Richard A. Burton
 */
#include <stdbool.h>
#include <stdint.h>
#include "zboot_private.h"
#include "zboot.h"
#include "espgpio.h"

#if BOOT_GPIO_NUM > 16
#error "Invalid BOOT_GPIO_NUM value (disable BOOT_GPIO_ENABLED to disable this feature)"
#endif

// -----------------------------------------------------------------------------------------------------------
// GPIO16

#define ETS_UNCACHED_ADDR(addr) (addr)
#define READ_PERI_REG(addr) (*((volatile uint32_t *)ETS_UNCACHED_ADDR(addr)))
#define WRITE_PERI_REG(addr, val) (*((volatile uint32_t *)ETS_UNCACHED_ADDR(addr))) = (uint32_t)(val)
#define PERIPHS_RTC_BASEADDR    0x60000700
#define REG_RTC_BASE            PERIPHS_RTC_BASEADDR
#define RTC_GPIO_OUT            (REG_RTC_BASE + 0x068)
#define RTC_GPIO_ENABLE         (REG_RTC_BASE + 0x074)
#define RTC_GPIO_IN_DATA        (REG_RTC_BASE + 0x08C)
#define RTC_GPIO_CONF           (REG_RTC_BASE + 0x090)
#define PAD_XPD_DCDC_CONF       (REG_RTC_BASE + 0x0A0)

static uint32_t get_gpio16(void)
{
   // set output level to 1
   WRITE_PERI_REG(RTC_GPIO_OUT, (READ_PERI_REG(RTC_GPIO_OUT) & (uint32_t)0xfffffffe) | (uint32_t)(1));

   // read level
   WRITE_PERI_REG(PAD_XPD_DCDC_CONF, (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32_t)0x1);	// mux configuration for XPD_DCDC and rtc_gpio0 connection
   WRITE_PERI_REG(RTC_GPIO_CONF, (READ_PERI_REG(RTC_GPIO_CONF) & (uint32_t)0xfffffffe) | (uint32_t)0x0);	//mux configuration for out enable
   WRITE_PERI_REG(RTC_GPIO_ENABLE, READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32_t)0xfffffffe);	//out disable

   return (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1);
}

// -----------------------------------------------------------------------------------------------------------
// All other GPIOs 

// support for "normal" GPIOs (other than 16)
#define REG_GPIO_BASE            0x60000300
#define GPIO_IN_ADDRESS          (REG_GPIO_BASE + 0x18)
#define GPIO_ENABLE_OUT_ADDRESS  (REG_GPIO_BASE + 0x0c)
#define REG_IOMUX_BASE           0x60000800
#define IOMUX_PULLUP_MASK        (1<<7)
#define IOMUX_FUNC_MASK          0x0130
const uint8_t IOMUX_REG_OFFS[] = {0x34, 0x18, 0x38, 0x14, 0x3c, 0x40, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x04, 0x08, 0x0c, 0x10};
const uint8_t IOMUX_GPIO_FUNC[] = {0x00, 0x30, 0x00, 0x30, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30};

static int get_gpio(int gpio_num)
{
   // disable output buffer if set
   uint32_t old_out = READ_PERI_REG(GPIO_ENABLE_OUT_ADDRESS);
   uint32_t new_out = old_out & ~ (1<<gpio_num);
   WRITE_PERI_REG(GPIO_ENABLE_OUT_ADDRESS, new_out);

   // set GPIO function, enable soft pullup
   uint32_t iomux_reg = REG_IOMUX_BASE + IOMUX_REG_OFFS[gpio_num];
   uint32_t old_iomux = READ_PERI_REG(iomux_reg);
   uint32_t gpio_func = IOMUX_GPIO_FUNC[gpio_num];
   uint32_t new_iomux = (old_iomux & ~IOMUX_FUNC_MASK) | gpio_func | IOMUX_PULLUP_MASK;
   WRITE_PERI_REG(iomux_reg, new_iomux);

   // allow soft pullup to take effect if line was floating
   ets_delay_us(10);
   int result = READ_PERI_REG(GPIO_IN_ADDRESS) & (1<<gpio_num);

   // set iomux & GPIO output mode back to initial values
   WRITE_PERI_REG(iomux_reg, old_iomux);
   WRITE_PERI_REG(GPIO_ENABLE_OUT_ADDRESS, old_out);
   return (result ? 1 : 0);
}

// -----------------------------------------------------------------------------------------------------------
// Exported functions 

bool gpio_asserted(uint8_t index)
{
   // pin low == GPIO boot
   if (index == 16)
      return (get_gpio16() == 0);
   else
      return (get_gpio(index) == 0);
}
