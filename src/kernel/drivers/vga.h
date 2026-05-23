#ifndef VGA_H
#define VGA_H

#include "../util/types.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_COLOR 0x07

static volatile u16* const VGA_BUFFER = (volatile u16*)0xB8000;

extern int cursor_pos;

void put_char(char c);
void print(const char* str);
void println(const char* str);
void clear_screen();
void update_cursor();

#endif
