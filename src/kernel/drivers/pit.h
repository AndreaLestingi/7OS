#pragma once
#include "util/types.h"

void pit_init(u32 frequency_hz);
void wait(int seconds);
void wait_ms(u32 ms);
extern volatile u32 pit_ticks;