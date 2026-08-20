#pragma once

#include <stdint.h>

/*
 * Board / front-panel wiring and compile-time limits.
 *
 * Everything hardware-specific lives here so re-pinning the controller is a
 * one-file edit. Footswitch N is logically paired with LED N (1-indexed on
 * the panel, 0-indexed in code).
 */

/* ---- Front panel ---------------------------------------------------------- */

#define NUM_SWITCHES 4u

/*
 * Footswitch GPIOs: momentary, normally-open to GND. We enable the internal
 * pull-up, so a press reads logic-0. Wire each switch between the pin and GND.
 */
#define FSW1_PIN 2u
#define FSW2_PIN 3u
#define FSW3_PIN 4u
#define FSW4_PIN 5u

/* LED GPIOs: active-high (pin high = lit). LED N pairs with FSW N. */
#define LED1_PIN 6u
#define LED2_PIN 7u
#define LED3_PIN 8u
#define LED4_PIN 9u

/* ---- Hardware MIDI out (5-pin DIN / TRS) ---------------------------------- */

/*
 * MIDI is output-only here. Drive MIDI_TX_PIN into a standard UART->MIDI
 * output stage (current-loop with a 5-pin DIN, or a TRS-A jack). 31250 baud,
 * 8N1, is the MIDI 1.0 wire rate.
 */
#define MIDI_UART    uart0
#define MIDI_TX_PIN  0u /* GP0 = UART0 TX */
#define MIDI_BAUD    31250u

/* ---- Config data-model limits (compile-time) ------------------------------ */

/* Max PC and CC messages a single switch press can emit. */
#define MAX_PC_PER_SWITCH 16u
#define MAX_CC_PER_SWITCH 16u

/*
 * Preset name buffer, bytes including the mandatory trailing NUL: up to 15
 * visible chars. Sized to one line of a 16-col LCD / SSD1306 OLED, which is
 * where this label is headed. Stored per preset in the flash blob.
 */
#define MAX_NAME_LEN 16u

/*
 * Banks held by the data model. All of them sit in the flash blob; which one is
 * live is config.active_bank, stepped by footswitch chord (controller.c) or set
 * by the web app. Raising this past 2*NUM_SWITCHES makes controller.c's LED
 * bank readout ambiguous - it encodes a bank as LED position plus blink/solid,
 * and that runs out at 8.
 */
#define NUM_BANKS 8u
