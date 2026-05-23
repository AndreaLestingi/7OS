ENTRY(_kernel_entry)
SECTIONS
{
    . = 0x8200;
    .text : {
        *(.text.entry)
        *(.text)
        *(.text*)
        *(.rodata*)
        *(.rdata*)
    }
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
        *(COMMON)
    }
}
