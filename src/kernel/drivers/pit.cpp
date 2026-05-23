#include "drivers/pit.h"
#include "drivers/vga.h"
#include "arch/i386/io.h"

#define PIT_FREQUENCY   1193182
#define PIT_CMD         0x43
#define PIT_CHANNEL0    0x40

volatile u32 pit_ticks = 0;

static inline void io_wait() {
    asm volatile ("outb %%al, $0x80" : : "a"(0));
}

void pit_init(u32 frequency_hz) {
    u16 divisor = (u16)(PIT_FREQUENCY / frequency_hz);
    outb(PIT_CMD, 0x36);
    io_wait();
    outb(PIT_CHANNEL0, (u8)(divisor & 0xFF));
    io_wait();
    outb(PIT_CHANNEL0, (u8)(divisor >> 8));
    io_wait();
}

void wait_ms(u32 ms) {
    for (u32 elapsed = 0; elapsed < ms; elapsed++) {
        for (volatile u32 i = 0; i < 50000; i = i + 1) {
            asm volatile ("nop");
        }
    }
}

void wait(int seconds) {
    wait_ms((u32)(seconds * 1000));
}

extern "C" void _pit_handler() {
    pit_ticks = pit_ticks + 1;
}

