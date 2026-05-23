#include "util.h"

struct E820Entry {
    unsigned long long base;
    unsigned long long length;
    u32 type;
    u32 acpi;
} __attribute__((packed));

#define E820_BUF_ADDR 0x5000
#define E820_COUNT_ADDR 0x4FF0
#define E820_MAX_ENTRIES 32

bool strcmp(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

void reverse(char* str, int len) {
    int start = 0;
    int end = len - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

void int_to_str(int n, char* str) {
    int i = 0;
    bool is_negative = false;

    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (n < 0) {
        is_negative = true;
        n = -n;
    }

    while (n != 0) {
        str[i++] = (n % 10) + '0';
        n = n / 10;
    }

    if (is_negative) str[i++] = '-';
    str[i] = '\0';
    reverse(str, i);
}

unsigned long long get_ram_bytes() {
    volatile u16* count = (volatile u16*)E820_COUNT_ADDR;
    volatile E820Entry* map = (volatile E820Entry*)E820_BUF_ADDR;

    u16 entries = *count;
    if (entries > E820_MAX_ENTRIES) {
        entries = E820_MAX_ENTRIES;
    }

    unsigned long long total = 0;
    for (u16 i = 0; i < entries; i++) {
        if (map[i].type == 1) {
            total += map[i].length;
        }
    }
    return total;
}

u32 get_ram_mb() {
    return (u32)(get_ram_bytes() >> 20);
}