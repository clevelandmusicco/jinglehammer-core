#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"

/*
 * Controller configuration: the set of MIDI messages each footswitch emits,
 * grouped into banks, persisted to flash, and (later) read/written by the web
 * app over USB.
 *
 * MIDI channels are stored 0..15 here - the value OR'd straight into a status
 * byte. The panel/web-app facing range is 1..16; translate at that edge, not
 * in here.
 */

/* A single Program Change message. */
typedef struct {
    uint8_t channel; /* 0..15 */
    uint8_t program; /* 0..127 */
} pc_msg_t;

/* A single Control Change message. */
typedef struct {
    uint8_t channel;    /* 0..15 */
    uint8_t controller; /* 0..127 */
    uint8_t value;      /* 0..127 */
} cc_msg_t;

/*
 * The bundle one footswitch press emits: a display label, then zero or more
 * Program Changes followed by zero or more Control Changes, each message free
 * to target its own channel.
 *
 * `name` is a NUL-terminated label for the front panel (a future OLED/LCD). It
 * is purely cosmetic - the firmware never acts on it - but config_is_valid()
 * still requires the terminator so any string read of it is bounded. All bytes
 * here are single-byte, so the struct stays densely packed with no padding.
 */
typedef struct {
    char     name[MAX_NAME_LEN];  /* NUL-terminated, <=15 visible chars */
    uint8_t  pc_count;            /* 0..MAX_PC_PER_SWITCH */
    pc_msg_t pc[MAX_PC_PER_SWITCH];
    uint8_t  cc_count;            /* 0..MAX_CC_PER_SWITCH */
    cc_msg_t cc[MAX_CC_PER_SWITCH];
} preset_t;

/*
 * Per-switch behaviour. `mode` is reserved for growth: today every switch is a
 * momentary preset (press fires `preset`, radio-style LED). A future build can
 * add latching/toggle modes without changing the flash layout.
 */
typedef enum {
    SW_MODE_PRESET = 0, /* momentary; press emits `preset`, this LED lit, others dark */
} switch_mode_t;

typedef struct {
    uint8_t  mode; /* switch_mode_t; reserved, defaults to SW_MODE_PRESET */
    preset_t preset;
} switch_cfg_t;

typedef struct {
    switch_cfg_t sw[NUM_SWITCHES];
} bank_cfg_t;

#define CONFIG_MAGIC   0x4D494443u /* "MIDC" */
#define CONFIG_VERSION 3u          /* v3: added preset_t.name */

/*
 * The whole persisted blob. Header first, CRC last (the CRC covers every byte
 * before it). Header fields are little-endian (RP2350 native); the body is all
 * single bytes, so the struct is densely packed with a stable layout that the
 * web app can mirror byte-for-byte.
 */
typedef struct {
    uint32_t   magic;
    uint16_t   version;
    uint16_t   active_bank; /* 0..NUM_BANKS-1 */
    bank_cfg_t bank[NUM_BANKS];
    uint32_t   crc32; /* over all bytes preceding this field */
} config_t;

/*
 * Live, in-RAM configuration. Loaded at boot, mutated by the (future) config
 * transport, written back to flash by config_save().
 */
extern config_t g_config;

void config_load_defaults(void); /* overwrite g_config with factory defaults */
bool config_load(void);          /* read flash into g_config; false if absent/invalid */
bool config_save(void);          /* stamp header+CRC, write g_config to flash */

/*
 * Serialisation seams for the deferred web-app transport. The wire format is
 * the little-endian config_t blob (CRC included). A CDC/SysEx layer calls these;
 * nothing here commits to a particular transport.
 */
size_t config_serialized_size(void);
size_t config_serialize(uint8_t *out, size_t cap);        /* bytes written, 0 if cap too small */
bool   config_deserialize(const uint8_t *in, size_t len); /* validate, then replace g_config */

/* Preset currently bound to footswitch `sw` (0..NUM_SWITCHES-1) in the active bank. */
static inline const preset_t *config_active_preset(uint8_t sw)
{
    return &g_config.bank[g_config.active_bank].sw[sw].preset;
}
