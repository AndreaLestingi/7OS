#include "drivers/vga.h"
#include "drivers/pit.h"
#include "drivers/ata.h"
#include "arch/i386/idt.h"

extern "C" void init();

extern "C" void _kmain(void) {
    idt_install();
    print("IDT ok\n");
    pit_init(1000);
    print("PIT ok\n");
    if (ata_init()) {
        print("ATA ok\n");
    } else {
        print("ATA no drive\n");
    }

    init();

    while (1) {
        asm volatile ("hlt");
    }
}