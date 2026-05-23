bits 32
section .text

[extern _idtp]
_idt_load:
    lidt [_idtp]
    ret

[extern _keyboard_handler_main]
_keyboard_handler_asm:
    pusha
    call _keyboard_handler_main
    mov al, 0x20
    out 0x20, al
    popa
    iretd

[extern _dummy_handler_main]
_dummy_handler_asm:
    pusha
    call _dummy_handler_main
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    popa
    iretd

[extern _pit_handler]
_pit_irq_wrapper:
    pusha
    call _pit_handler
    mov al, 0x20
    out 0x20, al
    popa
    iretd

global _idt_load
global _keyboard_handler_asm
global _dummy_handler_asm
global _pit_irq_wrapper