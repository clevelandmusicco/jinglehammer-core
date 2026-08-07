#pragma once

#include <stdint.h>

void controller_init(void);              /* establish the initial LED state */
void controller_on_edges(uint8_t edges); /* react to footswitch press edges (bitmask from io_poll) */
