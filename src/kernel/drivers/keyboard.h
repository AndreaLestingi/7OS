#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../util/string.h"

extern u8 kbd_map[128];
void keyboard_init();
void process_scancode(u8 scancode);
String readl();

#endif
