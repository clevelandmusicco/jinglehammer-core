#include "pico/stdlib.h"
#include "tusb.h"

#include "board.h"
#include "config.h"
#include "midi.h"
#include "io.h"
#include "controller.h"
#include "cdc_proto.h"

/* Footswitch poll period. Sets the debounce time base (see io.c). */
#define POLL_INTERVAL_MS 1

int main(void)
{
    /* TinyUSB (SDK 2.2.0 API): device role, speed auto-detected. */
    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    midi_init();
    io_init();

    /* First boot or a corrupt blob -> write factory defaults so flash is valid. */
    if (!config_load()) {
        config_load_defaults();
        config_save();
    }

    controller_init();

    absolute_time_t next_poll = make_timeout_time_ms(POLL_INTERVAL_MS);

    for (;;) {
        tud_task(); /* keep USB serviced as fast as the loop turns */

        /* Web-app config link: parse CDC command frames, drive g_config and
         * flash. Best-effort and non-blocking; it never stalls the pedalboard. */
        cdc_proto_task();

        /* Drain any inbound USB-MIDI we don't act on, for the same reason. */
        while (tud_midi_available()) {
            uint8_t pkt[4];
            tud_midi_packet_read(pkt);
        }

        if (time_reached(next_poll)) {
            uint8_t edges = io_poll();
            if (edges)
                controller_on_edges(edges);

            /*
             * Keep polls at >=1 ms real spacing. controller_on_edges() can block
             * for several ms (uart_write_blocking on a large CC bundle), and a
             * config_save() masks interrupts for tens of ms. Advancing purely from
             * the scheduled time would then leave next_poll in the past and fire
             * io_poll() back-to-back on catch-up, collapsing io.c's 8 ms debounce
             * window. So if we've fallen behind, re-anchor to now + 1 ms.
             */
            next_poll = delayed_by_ms(next_poll, POLL_INTERVAL_MS);
            if (time_reached(next_poll))
                next_poll = make_timeout_time_ms(POLL_INTERVAL_MS);
        }
    }
}
