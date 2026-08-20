#include "cdc_proto.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tusb.h"

#include "config.h"
#include "midi.h"

/* ---- Wire format ---------------------------------------------------------- */

#define SOF0 0x4Du /* 'M' */
#define SOF1 0x43u /* 'C' */

enum {
    CMD_HELLO   = 0x01, /* -> {proto_ver, config_ver(2), sizeof(config_t)(2)} */
    CMD_READ    = 0x02, /* -> config_t blob */
    CMD_WRITE   = 0x03, /* blob -> validate into RAM g_config (not flash) */
    CMD_SAVE    = 0x04, /* -> commit RAM g_config to flash */
    CMD_FACTORY = 0x05, /* -> load factory defaults into RAM */
    CMD_TEST    = 0x06, /* preset_t image -> emit it now (MIDI out); no state change */
};

enum {
    ST_OK         = 0,
    ST_BAD_LENGTH = 1, /* payload longer than any command expects */
    ST_REJECTED   = 2, /* config_deserialize/serialize refused */
    ST_SAVE_FAIL  = 3, /* flash write-back did not verify */
    ST_BAD_CMD    = 4,
};

#define PROTO_VERSION 1u

#define RESP_HDR 6u                                  /* M C cmd status len_lo len_hi */
#define RESP_CAP (RESP_HDR + sizeof(config_t))       /* READ is the largest response */
#define REQ_CAP  (sizeof(config_t))                  /* WRITE is the largest request  */

/* ---- TX: drain a response across loop ticks, never block the pedalboard --- */

static uint8_t  s_resp[RESP_CAP];
static uint32_t s_resp_len;
static uint32_t s_resp_sent;
static bool     s_resp_active;

static void begin_response(uint8_t cmd, uint8_t status, uint16_t payload_len)
{
    s_resp[0] = SOF0;
    s_resp[1] = SOF1;
    s_resp[2] = cmd;
    s_resp[3] = status;
    s_resp[4] = (uint8_t)(payload_len & 0xFFu);
    s_resp[5] = (uint8_t)((payload_len >> 8) & 0xFFu);
    s_resp_len    = RESP_HDR + payload_len;
    s_resp_sent   = 0;
    s_resp_active = true;
}

/* Drain TX buffer across ticks; CDC FIFO may not fit everything. */
static void pump_tx(void)
{
    while (s_resp_sent < s_resp_len) {
        uint32_t avail = tud_cdc_write_available();
        if (avail == 0)
            break;
        uint32_t chunk = s_resp_len - s_resp_sent;
        if (chunk > avail)
            chunk = avail;
        uint32_t w = tud_cdc_write(s_resp + s_resp_sent, chunk);
        s_resp_sent += w;
        if (w == 0)
            break;
    }
    tud_cdc_write_flush();
    if (s_resp_sent >= s_resp_len)
        s_resp_active = false;
}

/* ---- Command dispatch ----------------------------------------------------- */

static void dispatch(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    switch (cmd) {
    case CMD_HELLO: {
        uint8_t *p = s_resp + RESP_HDR;
        uint16_t blob = (uint16_t)sizeof(config_t);
        p[0] = PROTO_VERSION;
        p[1] = (uint8_t)(CONFIG_VERSION & 0xFFu);
        p[2] = (uint8_t)((CONFIG_VERSION >> 8) & 0xFFu);
        p[3] = (uint8_t)(blob & 0xFFu);
        p[4] = (uint8_t)((blob >> 8) & 0xFFu);
        begin_response(cmd, ST_OK, 5);
        break;
    }
    case CMD_READ: {
        size_t n = config_serialize(s_resp + RESP_HDR, sizeof s_resp - RESP_HDR);
        begin_response(cmd, n ? ST_OK : ST_REJECTED, (uint16_t)n);
        break;
    }
    case CMD_WRITE: {
        bool ok = config_deserialize(payload, len);
        begin_response(cmd, ok ? ST_OK : ST_REJECTED, 0);
        break;
    }
    case CMD_SAVE: {
        bool ok = config_save();
        begin_response(cmd, ok ? ST_OK : ST_SAVE_FAIL, 0);
        break;
    }
    case CMD_FACTORY:
        config_load_defaults(); /* RAM only; host READs back / SAVEs */
        begin_response(cmd, ST_OK, 0);
        break;
    case CMD_TEST: {
        /* Emit test preset without state change; image validated by midi_send_preset. */
        if (len != sizeof(preset_t)) {
            begin_response(cmd, ST_BAD_LENGTH, 0);
            break;
        }
        preset_t p;
        memcpy(&p, payload, sizeof p);
        midi_send_preset(&p);
        begin_response(cmd, ST_OK, 0);
        break;
    }
    default:
        begin_response(cmd, ST_BAD_CMD, 0);
        break;
    }
}

/* ---- RX: byte-at-a-time frame parser -------------------------------------- */

enum { RX_SOF0, RX_SOF1, RX_CMD, RX_LEN0, RX_LEN1, RX_PAYLOAD };

static uint8_t  s_req[REQ_CAP];
static int      s_state = RX_SOF0;
static uint8_t  s_cmd;
static uint16_t s_len;
static uint16_t s_got;
static bool     s_discard; /* oversize payload: count it out, don't buffer it */

static void feed_byte(uint8_t b)
{
    switch (s_state) {
    case RX_SOF0:
        if (b == SOF0)
            s_state = RX_SOF1;
        break;
    case RX_SOF1:
        if (b == SOF1)
            s_state = RX_CMD;
        else if (b != SOF0) /* allow 'M' 'M' 'C' resync */
            s_state = RX_SOF0;
        break;
    case RX_CMD:
        s_cmd   = b;
        s_state = RX_LEN0;
        break;
    case RX_LEN0:
        s_len   = b;
        s_state = RX_LEN1;
        break;
    case RX_LEN1:
        s_len |= (uint16_t)b << 8;
        s_got     = 0;
        s_discard = (s_len > sizeof s_req);
        if (s_len == 0) {
            dispatch(s_cmd, s_req, 0);
            s_state = RX_SOF0;
        } else {
            s_state = RX_PAYLOAD;
        }
        break;
    case RX_PAYLOAD:
        if (!s_discard)
            s_req[s_got] = b;
        if (++s_got >= s_len) {
            if (s_discard)
                begin_response(s_cmd, ST_BAD_LENGTH, 0);
            else
                dispatch(s_cmd, s_req, s_len);
            s_state = RX_SOF0;
        }
        break;
    }
}

/* ---- Loop entry ----------------------------------------------------------- */

void cdc_proto_task(void)
{
    /* Disconnect: abandon pending TX, drain RX to avoid host stall. */
    if (!tud_cdc_connected()) {
        s_resp_active = false;
        s_state       = RX_SOF0;
        if (tud_cdc_available()) {
            uint8_t junk[64];
            tud_cdc_read(junk, sizeof junk);
        }
        return;
    }

    /* One response in flight at a time: finish sending before reading more. */
    if (s_resp_active) {
        pump_tx();
        return;
    }

    /* Read one byte at a time to avoid stranding incomplete frames; stop when reply queued. */
    while (tud_cdc_available()) {
        uint8_t b;
        if (tud_cdc_read(&b, 1) != 1)
            break;
        feed_byte(b);
        if (s_resp_active) {
            pump_tx();
            return;
        }
    }
}
