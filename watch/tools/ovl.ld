/* overlay image: header first, then code, constants and data in one block; bss after it */
SECTIONS {
  . = 0;
  .text : {
    KEEP(*(.ovl_header))
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.data .data.*)
    . = ALIGN(4);
    __ovl_img_end = .;
  }
  .bss : { *(.bss .bss.*) *(COMMON) . = ALIGN(4); __ovl_end = .; }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
