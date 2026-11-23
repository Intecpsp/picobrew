OUTPUT_FORMAT("elf32-avr")
OUTPUT_ARCH(avr:51)
ENTRY(zymatic_setup)
MEMORY {
  crc_hole (rx) : ORIGIN = 0x2178, LENGTH = 0xfe
  low (rx) : ORIGIN = 0x12f2, LENGTH = 0x985
  app (rx) : ORIGIN = 0x1b920, LENGTH = 0x42e0
  globals (rw) : ORIGIN = 0x802c8a, LENGTH = 0x300
}
SECTIONS {
  .crc_hole : { KEEP(*(.crc_hole)) } > crc_hole
  .low : { KEEP(*(.low_helpers)) KEEP(*(.low_web)) . = ALIGN(2); *(.low_functions) } > low
  .text : {
    *(.text .text.*)
    *(.progmem .progmem.* .progmem1.data*)
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

/* Exact pinned AVR library routines already supplied by the OEM image. */
strcpy = 0x26c0;
__udivmodhi4 = 0x1b0ee;
__udivmodsi4 = 0x1b13c;
__epilogue_restores__ = 0x1b210;
__prologue_saves__ = 0x1b1d8;
