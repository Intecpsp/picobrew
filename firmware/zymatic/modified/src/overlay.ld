OUTPUT_FORMAT("elf32-avr")
OUTPUT_ARCH(avr:51)
ENTRY(zymatic_setup)
MEMORY {
  app (rx) : ORIGIN = 0x1bc00, LENGTH = 0x2400
  globals (rw) : ORIGIN = 0x802c8a, LENGTH = 0x300
}
SECTIONS {
  .text : {
    *(.text .text.*)
    *(.progmem .progmem.*)
    KEEP(*(.provision))
    . = ALIGN(2);
    KEEP(*(.image_crc))
  } > app
  .bss (NOLOAD) : {
    *(.bss .bss.* COMMON)
    __extension_bss_end = .;
  } > globals
  /* Constants in ordinary C address space would be treated as SRAM addresses;
     both regions require explicit placement and far flash reads on this MCU. */
  .unexpected : { *(.data .data.* .rodata .rodata.* .init* .fini* .eeprom*) } > globals
  ASSERT(SIZEOF(.unexpected) == 0, "Uninitialized AVR data or near-flash constants")
  /DISCARD/ : { *(.comment .note.*) }
}
oem_setup = 0x11168;
oem_serial_events = 0x1a1c6;
oem_millis = 0x19d94;
oem_button_event = 0x1122c;
