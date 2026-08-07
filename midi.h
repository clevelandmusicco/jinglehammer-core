#pragma once

#include "config.h"

void midi_init(void);                     /* bring up UART0 as 31250 8N1 MIDI out */
void midi_send_preset(const preset_t *p); /* emit PCs then CCs, to UART + USB */
