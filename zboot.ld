MEMORY
{
  dram_seg :  org = 0x3FFE8000, len = 0x14000   /* DRAM */
  iram_seg :  org = 0x40100000, len = 0x7F00    /* 32kB of internal RAM */
  iram1_seg : org = 0x4010C000, len = 0x4000    /* 16kB of internal RAM */
}

ENTRY(zboot_main)

SECTIONS
{
  .data : ALIGN(4)
  {
    _data_start = ABSOLUTE(.);
    *(.data)
    _data_end = ABSOLUTE(.);
  } >dram_seg

  .rodata : ALIGN(4)
  {
    _rodata_start = ABSOLUTE(.);
    *(.rodata)
    *(.rodata.*)
    *(.rodata1)
    . = ALIGN(4);		/* this table MUST be 4-byte aligned */
    _rodata_end = ABSOLUTE(.);
  } >dram_seg

  .bss ALIGN(8) (NOLOAD) : ALIGN(4)
  {
    . = ALIGN (8);
    _bss_start = ABSOLUTE(.);
    *(.bss)
    *(COMMON)
    . = ALIGN (8);
    _bss_end = ABSOLUTE(.);
    _heap_start = ABSOLUTE(.);
  } >dram_seg

  .text : ALIGN(4)
  {
    _stext = .;
    _text_start = ABSOLUTE(.);
    . = ALIGN (16);
    *(.literal .text .literal.* .text.*)
    _text_end = ABSOLUTE(.);
    _etext = .;
  } >iram_seg

  .final : ALIGN(4)
  {
    _final_start = ABSOLUTE(.);
    *(.final.text)
    _final_end = ABSOLUTE(.);
  } >iram1_seg
}

INCLUDE "eagle.rom.addr.v6.ld"  /* ROM function entrypoints */
