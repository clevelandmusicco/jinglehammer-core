#include "io.h"

#include "hardware/gpio.h"

#include "board.h"

static const uint8_t fsw_pin[NUM_SWITCHES] = { FSW1_PIN, FSW2_PIN, FSW3_PIN, FSW4_PIN };
static const uint8_t led_pin[NUM_SWITCHES] = { LED1_PIN, LED2_PIN, LED3_PIN, LED4_PIN };

/*
 * Per-switch 8-sample shift-register debounce. A state change registers only
 * after 8 consecutive equal samples; at the 1 ms poll rate that's an 8 ms guard
 * band, comfortably past typical footswitch contact bounce. Polling (vs a GPIO
 * IRQ) keeps all switch state out of interrupt context and makes timing
 * deterministic.
 */
static uint8_t hist[NUM_SWITCHES];
static bool    down[NUM_SWITCHES];

void io_init(void)
{
    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
        gpio_init(fsw_pin[i]);
        gpio_set_dir(fsw_pin[i], GPIO_IN);
        gpio_pull_up(fsw_pin[i]); /* normally-open to GND: pressed reads 0 */
        hist[i] = 0;
        down[i] = false;

        gpio_init(led_pin[i]);
        gpio_set_dir(led_pin[i], GPIO_OUT);
        gpio_put(led_pin[i], 0);
    }
}

uint8_t io_poll(void)
{
    uint8_t edges = 0;

    for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
        bool pressed = (gpio_get(fsw_pin[i]) == 0); /* active-low */
        hist[i] = (uint8_t)((hist[i] << 1) | (pressed ? 1u : 0u));

        if (!down[i] && hist[i] == 0xFFu) {        /* 8 stable presses -> press edge */
            down[i] = true;
            edges |= (uint8_t)(1u << i);
        } else if (down[i] && hist[i] == 0x00u) {  /* 8 stable releases */
            down[i] = false;
        }
    }

    return edges;
}

void io_set_leds(uint8_t mask)
{
    for (uint8_t i = 0; i < NUM_SWITCHES; i++)
        gpio_put(led_pin[i], (mask >> i) & 1u);
}
