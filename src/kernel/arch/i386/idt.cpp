#include "idt.h"
#include "io.h"

extern "C" {
    idt_entry idt[256];
    idt_ptr _idtp;
}

extern "C" void _pit_irq_wrapper();
extern "C" void _keyboard_handler_asm();
extern "C" void _dummy_handler_asm();

void idt_set_gate(u8 num, u32 base, u16 sel, u8 flags) {
    idt[num].base_low  = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

void idt_install() {
    _idtp.limit = (sizeof(idt_entry) * 256) - 1;
    _idtp.base  = (u32)&idt;

    for (int i = 0; i < 256; i++)
        idt_set_gate(i, (u32)_dummy_handler_asm, 0x08, 0x8E);

    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);

    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);

    idt_set_gate(32, (u32)_pit_irq_wrapper,      0x08, 0x8E);
    idt_set_gate(33, (u32)_keyboard_handler_asm, 0x08, 0x8E);

    _idt_load();
}