#include "controller.h"

#include "config.h"
#include "midi.h"
#include "io.h"

/*
 * Momentary-preset ("radio") policy: a press emits that switch's bundle and
 * makes it the single active preset - its LED lights, the others go dark. At
 * boot nothing is active (all LEDs off), so the unit stays silent until the
 * first press.
 *
 * This file is where future switch behaviours land (latching/toggle via
 * switch_cfg.mode, bank up/down, hold/double-tap), so the policy stays in one
 * place rather than bleeding into main().
 */

void controller_init(void)
{
    io_set_leds(0);
}

void controller_on_edges(uint8_t edges)
{
    int last = -1;

    for (uint8_t m = edges; m != 0; m &= (uint8_t)(m - 1)) {
        uint8_t i = (uint8_t)__builtin_ctz(m); /* lowest set bit = lowest switch index */
        midi_send_preset(config_active_preset(i));
        last = i;
    }

    /* Radio: if any fired, light only the last-pressed switch's LED. */
    if (last >= 0)
        io_set_leds((uint8_t)(1u << last));
}
