#include "controller.h"

#include "pico/time.h"

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
 * switch_cfg.mode, bank up/down, hold), so the policy stays in one place
 * rather than bleeding into main().
 *
 * Bank navigation: pressing SW1+SW2 together steps the active bank down;
 * SW3+SW4 together steps it up. A chord suppresses both switches' normal
 * preset fire entirely - only the bank step and its LED display happen, so
 * there's no MIDI collision with whatever presets those switches carry.
 *
 * Chord detection needs a short tolerance window rather than requiring both
 * edges in the exact same io_poll() tick: each switch debounces independently
 * (8 ms shift register, see io.c), so two real footswitch presses aimed at
 * "the same moment" can complete a tick or two apart. When an edge lands on a
 * switch that's half of a chord pair, it's held pending for CHORD_WINDOW_MS:
 * if the partner edge shows up (this tick or a later one) before the deadline,
 * it's a chord - bank step fires, neither preset does. If the deadline passes
 * first, it was a solo press - the queued preset fires then, late by at most
 * CHORD_WINDOW_MS. This puts a small, constant latency on every press of
 * SW1-4 (all four are chord members), traded for chords being reliably
 * hittable by foot.
 *
 * Bank display: a recognised chord repurposes the LEDs for
 * BANK_DISPLAY_WINDOW_MS to show the new active bank as a binary-ish
 * position/blink code - LED (bank % NUM_SWITCHES) lit, solid for banks
 * 1..NUM_SWITCHES, blinking for banks NUM_SWITCHES+1..2*NUM_SWITCHES (encoding
 * only unambiguous up to 2*NUM_SWITCHES banks; NUM_BANKS is exactly that
 * today). A later chord while the display is showing restarts it against the
 * new bank; any normal switch press preempts it immediately, since a fired
 * preset's radio LED always wins over a stale bank indicator.
 *
 * Bank change lands in RAM immediately, but nothing on the new bank is
 * "active" yet - stepping banks does not imply picking preset 1. Once the
 * bank-display window ends, all 4 LEDs breathe (fade low->high, then
 * high->low, repeat) for PRESET_WAIT_WINDOW_MS while the unit waits for the
 * player to pick a preset on the new bank (any normal press, chord or solo,
 * preempts the fade exactly like it preempts the display). If the window
 * lapses with nothing picked, the whole navigation is abandoned: active_bank
 * reverts to whatever it was before this chord sequence started - "no pick"
 * means "never left." The revert isn't silent: it lands the old bank number
 * in RAM and replays the normal bank-display readout for it (same position/
 * blink code as any other bank change), then settles on the LED of whichever
 * preset was actually active before all this started (or off, pre-first-
 * press) - so a revert reads as "back to bank 5, preset 3" rather than an
 * unexplained snap to some LED state.
 */

#define CHORD_WINDOW_MS          20u /* 8 ms debounce + slack for real-world press skew */
#define BANK_DISPLAY_WINDOW_MS   800u
#define BANK_BLINK_INTERVAL_MS   100u /* 4 on/off cycles fill the 800 ms window exactly */
#define PRESET_WAIT_WINDOW_MS   2000u
#define PRESET_WAIT_STATE_MS     500u /* one fade ramp (low->high or high->low); 2 up+down cycles fill the 2000 ms window exactly */
#define PRESET_WAIT_LEVEL_LOW      8u /* dim, not fully off - the floor of the breathing fade */
#define PRESET_WAIT_LEVEL_HIGH   255u /* full brightness (LED_PWM_WRAP in io.c) */

/* Switches pair up as (0,1) and (2,3) - i^1 gives the chord partner for both. */
static bool             pending[NUM_SWITCHES];
static absolute_time_t  pending_deadline[NUM_SWITCHES];

typedef enum {
    LED_MODE_RADIO,        /* LEDs reflect the last-fired preset (normal operation) */
    LED_MODE_BANK_DISPLAY, /* LEDs temporarily show the active bank instead */
    LED_MODE_AWAIT_PRESET, /* all LEDs breathe (fade), waiting for a preset pick on the new bank */
} led_mode_t;

static led_mode_t      led_mode;
static absolute_time_t bank_display_deadline;
static uint8_t          bank_display_pos;   /* 0..NUM_SWITCHES-1 */
static bool             bank_display_blink;
static absolute_time_t bank_blink_next_toggle;
static bool             bank_blink_led_on;
static bool             bank_display_revert; /* true = this display is showing the bank we're reverting to, not one just navigated to */

static absolute_time_t preset_wait_deadline;
static absolute_time_t preset_wait_state_start; /* start of the current 500 ms ramp */
static absolute_time_t preset_wait_state_end;   /* scheduled end of the current ramp */
static bool             preset_wait_rising;      /* true = fading low->high this ramp */

/* Bank active before the current chord sequence started, and the last switch
 * whose preset actually fired (-1 = none yet, e.g. fresh boot). Both restored
 * verbatim if PRESET_WAIT_WINDOW_MS lapses with no pick. */
static uint16_t pre_nav_bank;
static int8_t   active_switch = -1;

static void bank_step(int8_t delta)
{
    int32_t bank = (int32_t)g_config.active_bank + delta;

    if (bank < 0)
        bank = 0;
    else if (bank > (int32_t)NUM_BANKS - 1)
        bank = (int32_t)NUM_BANKS - 1;

    g_config.active_bank = (uint16_t)bank;
}

static void start_bank_display(bool is_revert)
{
    uint16_t bank = g_config.active_bank;

    bank_display_pos      = (uint8_t)(bank % NUM_SWITCHES);
    bank_display_blink    = (bank / NUM_SWITCHES) != 0;
    bank_display_deadline = make_timeout_time_ms(BANK_DISPLAY_WINDOW_MS);
    bank_display_revert   = is_revert;
    led_mode              = LED_MODE_BANK_DISPLAY;

    bank_blink_led_on      = true;
    bank_blink_next_toggle = make_timeout_time_ms(BANK_BLINK_INTERVAL_MS);
    io_set_leds((uint8_t)(1u << bank_display_pos));
}

static void start_preset_wait(void)
{
    led_mode             = LED_MODE_AWAIT_PRESET;
    preset_wait_deadline = make_timeout_time_ms(PRESET_WAIT_WINDOW_MS);

    preset_wait_rising      = true;
    preset_wait_state_start = get_absolute_time();
    preset_wait_state_end   = make_timeout_time_ms(PRESET_WAIT_STATE_MS);
    io_set_leds_level(PRESET_WAIT_LEVEL_LOW);
}

void controller_init(void)
{
    io_set_leds(0);
    for (uint8_t i = 0; i < NUM_SWITCHES; i++)
        pending[i] = false;
    led_mode      = LED_MODE_RADIO;
    active_switch = -1;
}

void controller_on_edges(uint8_t edges)
{
    uint8_t handled      = 0;
    bool    chord_fired = false;

    /* Snapshot before any bank_step() below - only a fresh (non-navigating)
     * sequence updates pre_nav_bank, so a chord pressed again mid-display or
     * mid-wait keeps chaining against the bank active before the *first*
     * chord, not the intermediate one. */
    bool     fresh_nav   = (led_mode == LED_MODE_RADIO);
    uint16_t bank_before = g_config.active_bank;

    for (uint8_t m = edges; m != 0; m &= (uint8_t)(m - 1)) {
        uint8_t i = (uint8_t)__builtin_ctz(m); /* lowest set bit = lowest switch index */

        if (handled & (1u << i))
            continue;

        uint8_t partner = i ^ 1u;
        int8_t  delta    = (i < 2) ? -1 : +1; /* SW1+2 = bank down, SW3+4 = bank up */

        if (edges & (1u << partner)) {
            /* Both halves of the chord landed in this same poll tick. */
            handled |= (uint8_t)((1u << i) | (1u << partner));
            bank_step(delta);
            chord_fired = true;
        } else if (pending[partner] && !time_reached(pending_deadline[partner])) {
            /* Partner's edge arrived on an earlier tick and is still waiting
             * (deadline check matters when a blocking op, e.g. flash save,
             * stalls the loop long enough for a stale pending flag to survive
             * past its own window). */
            pending[partner] = false;
            bank_step(delta);
            chord_fired = true;
        } else {
            pending[i]          = true;
            pending_deadline[i] = make_timeout_time_ms(CHORD_WINDOW_MS);
        }
    }

    if (chord_fired) {
        if (fresh_nav)
            pre_nav_bank = bank_before;
        start_bank_display(false);
    }
}

/* Call every ~1 ms regardless of edges - fires presets whose chord window
 * expired with no partner, and drives the bank-display / preset-wait
 * timeouts and blink. */
void controller_poll(void)
{
    int last = -1;

    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
        if (pending[i] && time_reached(pending_deadline[i])) {
            pending[i] = false;
            midi_send_preset(config_active_preset(i));
            last = i;
        }
    }

    if (last >= 0) {
        /* A fired preset's radio LED always wins over a stale bank indicator
         * or preset-wait pulse - and it settles the nav, so no revert. */
        led_mode      = LED_MODE_RADIO;
        active_switch = (int8_t)last;
        io_set_leds((uint8_t)(1u << last));
        return;
    }

    if (led_mode == LED_MODE_BANK_DISPLAY) {
        if (time_reached(bank_display_deadline)) {
            if (bank_display_revert) {
                /* Revert's bank display has had its say - now show which
                 * preset is (still) active on that bank, same as any other
                 * settle into radio mode. */
                led_mode = LED_MODE_RADIO;
                io_set_leds(active_switch >= 0 ? (uint8_t)(1u << active_switch) : 0);
            } else {
                start_preset_wait();
            }
            return;
        }
        if (bank_display_blink && time_reached(bank_blink_next_toggle)) {
            bank_blink_led_on = !bank_blink_led_on;
            io_set_leds(bank_blink_led_on ? (uint8_t)(1u << bank_display_pos) : 0);
            bank_blink_next_toggle = delayed_by_ms(bank_blink_next_toggle, BANK_BLINK_INTERVAL_MS);
        }
        return;
    }

    if (led_mode == LED_MODE_AWAIT_PRESET) {
        if (time_reached(preset_wait_deadline)) {
            /* Nothing picked in time - abandon the nav, back to how it was.
             * Land the bank in RAM immediately, then replay the same
             * bank-display readout for it (now the "old" bank again) so a
             * revert looks like a deliberate nav back, not a silent snap. */
            g_config.active_bank = pre_nav_bank;
            start_bank_display(true);
            return;
        }

        if (time_reached(preset_wait_state_end)) {
            preset_wait_rising      = !preset_wait_rising;
            /* Chain off the scheduled edge, not "now" - keeps ramp cadence
             * exact instead of drifting by however late this poll tick ran. */
            preset_wait_state_start = preset_wait_state_end;
            preset_wait_state_end   = delayed_by_ms(preset_wait_state_end, PRESET_WAIT_STATE_MS);
        }

        /* Recomputed every ~1 ms tick so the fade reads as continuous rather
         * than stepping only at ramp boundaries. */
        int64_t elapsed_ms = absolute_time_diff_us(preset_wait_state_start, get_absolute_time()) / 1000;
        if (elapsed_ms < 0)
            elapsed_ms = 0;
        else if (elapsed_ms > (int64_t)PRESET_WAIT_STATE_MS)
            elapsed_ms = PRESET_WAIT_STATE_MS;

        uint32_t span  = PRESET_WAIT_LEVEL_HIGH - PRESET_WAIT_LEVEL_LOW;
        uint32_t frac  = (uint32_t)((uint64_t)elapsed_ms * span / PRESET_WAIT_STATE_MS);
        uint8_t  level = preset_wait_rising
            ? (uint8_t)(PRESET_WAIT_LEVEL_LOW + frac)
            : (uint8_t)(PRESET_WAIT_LEVEL_HIGH - frac);

        io_set_leds_level(level);
    }
}
