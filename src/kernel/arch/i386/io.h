#ifndef IO_H
#define IO_H

#include "../../util/types.h"

extern "C" {
    inline void outb(u16 port, u8 val) {
        asm volatile ("outb %b0, %w1" : : "a"(val), "d"(port));
    }

    inline void outw(u16 port, u16 val) {
        asm volatile ("outw %w0, %w1" : : "a"(val), "d"(port));
    }

    inline u8 inb(u16 port) {
        u8 ret;
        asm volatile ("inb %w1, %b0" : "=a"(ret) : "d"(port));
        return ret;
    }

    inline u16 inw(u16 port) {
        u16 ret;
        asm volatile ("inw %w1, %w0" : "=a"(ret) : "d"(port));
        return ret;
    }
}

#endif
