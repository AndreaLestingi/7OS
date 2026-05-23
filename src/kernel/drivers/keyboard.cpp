#include "keyboard.h"
#include "vga.h"
#include "../util/util.h"
#include "../arch/i386/io.h"

u8 kbd_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`', 0,
    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-', 0,  '*', 0,  ' '
};

u8 kbd_map_shift[128] = {
    0,   27,  '!', '"', 0,   '$', '%', '&', '/', '(', ')', '=', '?', '^', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0,   '*', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0,   0,   '|', 0,
    0,   'Z', 'X', 'C', 'V', 'B', 'N', 'M', ';', ':', '_', 0,   '*', 0,  ' '
};

static bool shift_pressed = false;
static char input_buffer[256];
static int input_ptr = 0;

String readl() {
    u8 saved_mask = inb(0x21);
    outb(0x21, saved_mask | 0x02);

    char buffer[256];
    int length = 0;
    buffer[0] = '\0';

    while(true) {
        if (inb(0x64) & 1) {
            u8 scancode = inb(0x60);
            bool released = (scancode & 0x80) != 0;
            u8 key = scancode & 0x7F;

            if (key == 0x2A || key == 0x36) {
                shift_pressed = !released;
                continue;
            }

            if (!released) {
                char c = shift_pressed ? (char)kbd_map_shift[key] : (char)kbd_map[key];
                if (c == '\n') {
                    put_char('\n');
                    outb(0x21, saved_mask);
                    return String(buffer);
                } else if (c == '\b') {
                    if (length > 0) {
                        length--;
                        buffer[length] = '\0';
                        put_char('\b');
                    }
                } else if (c && length < 255) {
                    buffer[length++] = c;
                    buffer[length] = '\0';
                    put_char(c);
                }
            }
        }
    }
}

void handle_command() {
    input_buffer[input_ptr] = '\0';
    if (strcmp(input_buffer, "/help")) {
        print("\n7OS HELP: DIGITA PER SCRIVERE. BACKSPACE PER CANCELLARE.\n");
    } else if (strcmp(input_buffer, "/test")) {
        char buf[12];
        int_to_str(-1234, buf);
        print("\nNUMERO TEST: ");
        print(buf);
        print("\n");
    }
    input_ptr = 0;
    put_char('\n');
}

void process_scancode(u8 scancode) {
    bool released = (scancode & 0x80) != 0;
    u8 key = scancode & 0x7F;

    if (key == 0x2A || key == 0x36) {
        shift_pressed = !released;
        return;
    }

    if (!released) {
        char c = shift_pressed ? (char)kbd_map_shift[key] : (char)kbd_map[key];
        if (c == '\n') {
            handle_command();
        } else if (c == '\b') {
            if (input_ptr > 0) {
                input_ptr--;
                put_char('\b');
            }
        } else if (c && input_ptr < 255) {
            input_buffer[input_ptr++] = c;
            put_char(c);
        }
    }
}

extern "C" {
    void _keyboard_handler_main() {
        u8 scancode = inb(0x60);
        process_scancode(scancode);
    }

    void _dummy_handler_main() {
    }
}
