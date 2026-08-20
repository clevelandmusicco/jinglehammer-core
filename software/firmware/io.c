#include "io.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "board.h"

static const uint8_t fsw_pin[NUM_SWITCHES] = { FSW1_PIN, FSW2_PIN, FSW3_PIN, FSW4_PIN };
static const uint8_t led_pin[NUM_SWITCHES] = { LED1_PIN, LED2_PIN, LED3_PIN, LED4_PIN };

/*
 * PWM for fade support; 8-bit wrap=255 maps to uint8_t level. clkdiv=250 ->
 * ~2.3 kHz (past flicker fusion). LED pairs share PWM slices but have
 * independent duty.
 */
#define LED_PWM_WRAP   255u
#define LED_PWM_CLKDIV 250u

/*
 * 8-sample shift-register debounce (~8 ms at 1 ms poll). Polling keeps
 * state deterministic, out of IRQ context.
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

        gpio_set_function(led_pin[i], GPIO_FUNC_PWM);

        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv_int(&cfg, LED_PWM_CLKDIV);
        pwm_config_set_wrap(&cfg, LED_PWM_WRAP);
        /* pwm_init() resets both channels; confined to init only (set level
         * immediately after). */
        pwm_init(pwm_gpio_to_slice_num(led_pin[i]), &cfg, true);
        pwm_set_gpio_level(led_pin[i], 0);
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
        pwm_set_gpio_level(led_pin[i], ((mask >> i) & 1u) ? LED_PWM_WRAP : 0);
}

void io_set_leds_level(uint8_t level)
{
    for (uint8_t i = 0; i < NUM_SWITCHES; i++)
        pwm_set_gpio_level(led_pin[i], level);
}
