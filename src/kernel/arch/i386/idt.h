#ifndef IDT_H
#define IDT_H

#include "../../util/types.h"

struct idt_entry {
    u16 base_low;
    u16 sel;
    u8 always0;
    u8 flags;
    u16 base_high;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

extern "C" {
    extern idt_entry idt[256];
    extern idt_ptr _idtp;
    void _idt_load();
    void _keyboard_handler_asm();
    void _dummy_handler_asm();
}

void idt_install();
void idt_set_gate(u8 num, u32 base, u16 sel, u8 flags);

#endif
