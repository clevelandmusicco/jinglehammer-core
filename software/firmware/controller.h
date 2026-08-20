#pragma once

#include <stdint.h>

void controller_init(void);              /* establish the initial LED state */
void controller_on_edges(uint8_t edges); /* react to footswitch press edges (bitmask from io_poll) */
void controller_poll(void);              /* call ~every 1 ms; drives chord-window expiry, bank-display, and preset-wait timeout/blink */
