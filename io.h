#pragma once

#include <stdint.h>

void    io_init(void);             /* configure footswitch inputs and LED outputs */
uint8_t io_poll(void);             /* call ~every 1 ms; returns a press-edge bitmask (bit i = switch i) */
void    io_set_leds(uint8_t mask); /* bit i lit/unlit drives LED i */
