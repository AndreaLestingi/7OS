#ifndef UTIL_H
#define UTIL_H

#include "types.h"

bool strcmp(const char* s1, const char* s2);
void reverse(char* str, int len);
void int_to_str(int n, char* str);
unsigned long long get_ram_bytes();
u32 get_ram_mb();

#endif
