#include "midi.h"

#include "tusb.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "board.h"

/*
 * UART blocking (authoritative for pedals). USB-MIDI best-effort (non-blocking,
 * drops on full FIFO). CDC config separate, never blocks pedals.
 */
static void emit(const uint8_t *bytes, size_t n)
{
    uart_write_blocking(MIDI_UART, bytes, n);
    (void)tud_midi_stream_write(0, bytes, n); /* best-effort; see note above */
}

static void send_pc(uint8_t ch, uint8_t program)
{
    const uint8_t msg[2] = {
        (uint8_t)(0xC0u | (ch & 0x0Fu)),
        (uint8_t)(program & 0x7Fu),
    };
    emit(msg, sizeof msg);
}

static void send_cc(uint8_t ch, uint8_t controller, uint8_t value)
{
    const uint8_t msg[3] = {
        (uint8_t)(0xB0u | (ch & 0x0Fu)),
        (uint8_t)(controller & 0x7Fu),
        (uint8_t)(value & 0x7Fu),
    };
    emit(msg, sizeof msg);
}

void midi_init(void)
{
    uart_init(MIDI_UART, MIDI_BAUD);
    gpio_set_function(MIDI_TX_PIN, GPIO_FUNC_UART);
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART, true);
    /* Output only: the RX path is intentionally left unconfigured. */
}

void midi_send_preset(const preset_t *p)
{
    /* PCs before CCs: pedals typically recall a patch on PC, then trim it with CC. */
    uint8_t npc = p->pc_count;
    if (npc > MAX_PC_PER_SWITCH)
        npc = MAX_PC_PER_SWITCH; /* defensive: a bad pc_count must not walk off the array */
    for (uint8_t i = 0; i < npc; i++)
        send_pc(p->pc[i].channel, p->pc[i].program);

    uint8_t ncc = p->cc_count;
    if (ncc > MAX_CC_PER_SWITCH)
        ncc = MAX_CC_PER_SWITCH; /* defensive: a bad cc_count must not walk off the array */
    for (uint8_t i = 0; i < ncc; i++)
        send_cc(p->cc[i].channel, p->cc[i].controller, p->cc[i].value);
}
