#include "vga.h"
#include "../arch/i386/io.h"

int cursor_pos = 0;

void update_cursor() {
    u16 pos = (u16)cursor_pos;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

void scroll() {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
    }
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        VGA_BUFFER[i] = (VGA_COLOR << 8) | ' ';
    }
    cursor_pos -= VGA_WIDTH;
}

void put_char(char c) {
    if (c == '\n') {
        cursor_pos = (cursor_pos / VGA_WIDTH + 1) * VGA_WIDTH;
    } else if (c == '\b') {
        if (cursor_pos > 0) {
            cursor_pos--;
            VGA_BUFFER[cursor_pos] = (VGA_COLOR << 8) | ' ';
        }
    } else {
        VGA_BUFFER[cursor_pos++] = (VGA_COLOR << 8) | c;
    }

    while (cursor_pos >= VGA_WIDTH * VGA_HEIGHT) {
        scroll();
    }
    update_cursor();
}

void print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) put_char(str[i]);
}

void println(const char* str) {
    print(str);
    put_char('\n');
}

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) VGA_BUFFER[i] = (VGA_COLOR << 8) | ' ';
    cursor_pos = 0;
    update_cursor();
}
