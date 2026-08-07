#include "config.h"

#include <string.h>

#include "pico/stdlib.h"     /* XIP_BASE, PICO_FLASH_SIZE_BYTES */
#include "hardware/flash.h"  /* flash_range_*, FLASH_SECTOR_SIZE, FLASH_PAGE_SIZE */
#include "hardware/sync.h"   /* save_and_disable_interrupts / restore_interrupts */

config_t g_config;

/*
 * Reserve the last flash sector for config. One 4 KiB sector holds the blob
 * comfortably; the asserts below fail the build if NUM_BANKS / MAX_CC_PER_SWITCH
 * ever grow it past a sector, at which point raise CONFIG_FLASH_BYTES (in whole
 * sectors) and you're done - the layout is unchanged.
 */
#define CONFIG_FLASH_BYTES  FLASH_SECTOR_SIZE
#define CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - CONFIG_FLASH_BYTES)

/* flash_range_program writes whole 256-byte pages, so round the blob up. */
#define CONFIG_PROG_LEN \
    (((sizeof(config_t) + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE)

/* CRC covers every byte up to (not including) the trailing crc32 field. */
#define CONFIG_CRC_LEN (offsetof(config_t, crc32))

_Static_assert(CONFIG_PROG_LEN <= CONFIG_FLASH_BYTES,
               "config blob no longer fits the reserved flash region; raise CONFIG_FLASH_BYTES");
_Static_assert(CONFIG_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0,
               "config flash region must be sector-aligned");

/*
 * Pin the wire/flash layout: the web app mirrors config_t byte-for-byte and the
 * CRC math assumes crc32 is the trailing field with no padding before it. If a
 * future field edit introduces padding or reorders anything, these fail the build
 * instead of silently breaking the on-flash format.
 */
_Static_assert(sizeof(pc_msg_t) == 2u, "pc_msg_t must pack to 2 bytes");
_Static_assert(sizeof(cc_msg_t) == 3u, "cc_msg_t must pack to 3 bytes");
_Static_assert(sizeof(preset_t) == MAX_NAME_LEN + 2u + 2u * MAX_PC_PER_SWITCH + 3u * MAX_CC_PER_SWITCH,
               "preset_t gained padding");
_Static_assert(sizeof(switch_cfg_t) == 1u + sizeof(preset_t), "switch_cfg_t gained padding");
_Static_assert(offsetof(config_t, crc32) == sizeof(config_t) - 4u,
               "crc32 must be the trailing field with no padding before it");

/* Standard CRC-32 (reflected, poly 0xEDB88320). Runs only on save/load/validate. */
static uint32_t crc32_calc(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

static bool config_is_valid(const config_t *c)
{
    if (c->magic != CONFIG_MAGIC ||
        c->version != CONFIG_VERSION ||
        c->active_bank >= NUM_BANKS ||
        c->crc32 != crc32_calc(c, CONFIG_CRC_LEN))
        return false;

    /*
     * Integrity is good; now enforce the field ranges the data model documents,
     * so neither a corrupt flash blob nor a buggy/hostile web-app payload can
     * install out-of-spec config. pc_count and cc_count are the load-bearing
     * checks: they bound every pc[]/cc[] iteration, so validating them here keeps
     * that invariant at the trust boundary instead of relying on each downstream
     * consumer to clamp. mode is intentionally not range-checked - new
     * switch_mode_t values are a planned forward-compat extension, not an error.
     */
    for (uint8_t b = 0; b < NUM_BANKS; b++) {
        for (uint8_t s = 0; s < NUM_SWITCHES; s++) {
            const preset_t *p = &c->bank[b].sw[s].preset;
            /* Terminator must sit inside the buffer so a later string read of
             * name (front-panel display) is bounded. Printable-ness is the
             * editor's job, like the 1..16 channel translation. */
            if (p->name[MAX_NAME_LEN - 1] != 0)
                return false;
            if (p->pc_count > MAX_PC_PER_SWITCH || p->cc_count > MAX_CC_PER_SWITCH)
                return false;
            for (uint8_t i = 0; i < p->pc_count; i++)
                if (p->pc[i].channel > 15u || p->pc[i].program > 127u)
                    return false;
            for (uint8_t i = 0; i < p->cc_count; i++)
                if (p->cc[i].channel > 15u || p->cc[i].controller > 127u || p->cc[i].value > 127u)
                    return false;
        }
    }
    return true;
}

static void config_stamp(config_t *c)
{
    c->magic   = CONFIG_MAGIC;
    c->version = CONFIG_VERSION;
    c->crc32   = crc32_calc(c, CONFIG_CRC_LEN);
}

void config_load_defaults(void)
{
    memset(&g_config, 0, sizeof g_config); /* mode=SW_MODE_PRESET, pc_count=0, cc_count=0 everywhere */
    g_config.active_bank = 0;

    /*
     * Bank 0 ships with the worked example from the design brief, so a fresh
     * unit does something useful immediately. Channels are 0-indexed (ch1 -> 0):
     * switch 1 sends a PC on ch1 plus three CCs on ch2/ch3.
     */
    bank_cfg_t *b = &g_config.bank[0];

    /* memset above already NUL-terminated every name; strncpy(.., LEN-1) keeps
     * the last byte 0, so config_is_valid's terminator check holds. */
    static const char *const def_name[NUM_SWITCHES] = {
        "Preset 1", "Preset 2", "Preset 3", "Preset 4"
    };

    preset_t *p0 = &b->sw[0].preset;
    strncpy(p0->name, def_name[0], MAX_NAME_LEN - 1);
    p0->pc_count = 1;
    p0->pc[0]    = (pc_msg_t){ .channel = 0, .program = 0 }; /* ch1 */
    p0->cc_count = 3;
    p0->cc[0] = (cc_msg_t){ .channel = 1, .controller = 22, .value = 63 }; /* ch2 */
    p0->cc[1] = (cc_msg_t){ .channel = 2, .controller = 40, .value = 25 }; /* ch3 */
    p0->cc[2] = (cc_msg_t){ .channel = 2, .controller = 41, .value = 80 }; /* ch3 */

    /* Switches 2..4: a single PC on ch1 (program = switch index) to show the
     * radio behaviour - press one, its LED lights and the others go dark. */
    for (uint8_t i = 1; i < NUM_SWITCHES; i++) {
        preset_t *p = &b->sw[i].preset;
        strncpy(p->name, def_name[i], MAX_NAME_LEN - 1);
        p->pc_count = 1;
        p->pc[0]    = (pc_msg_t){ .channel = 0, .program = i };
        p->cc_count = 0;
    }

    config_stamp(&g_config);
}

bool config_load(void)
{
    /* Flash is XIP-mapped and sector-aligned, so this read is aligned and free. */
    const config_t *flash = (const config_t *)(XIP_BASE + CONFIG_FLASH_OFFSET);
    if (!config_is_valid(flash))
        return false;
    memcpy(&g_config, flash, sizeof g_config);
    return true;
}

bool config_save(void)
{
    config_stamp(&g_config);

    /*
     * flash_range_program reads exactly CONFIG_PROG_LEN bytes, so stage the blob
     * in a page-padded buffer rather than reading past g_config. Static (not
     * stack) - it is larger than the default 2 KiB stack.
     */
    static uint8_t page_buf[CONFIG_PROG_LEN];
    memset(page_buf, 0, sizeof page_buf);
    memcpy(page_buf, &g_config, sizeof g_config);

    /*
     * Single core: masking interrupts is enough. The SDK copies the flash
     * routines + stage-2 bootloader into SRAM and re-enters XIP afterwards, so
     * this is safe to call from flash-resident code. USB is unresponsive for the
     * duration of the erase (tens of ms) - fine for a rare, user-driven save.
     * If core1 is ever run from flash, switch to pico/flash.h flash_safe_execute().
     */
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, CONFIG_FLASH_BYTES);
    flash_range_program(CONFIG_FLASH_OFFSET, page_buf, sizeof page_buf);
    restore_interrupts(ints);

    /* Confirm the write took. */
    return config_is_valid((const config_t *)(XIP_BASE + CONFIG_FLASH_OFFSET));
}

size_t config_serialized_size(void)
{
    return sizeof(config_t);
}

size_t config_serialize(uint8_t *out, size_t cap)
{
    if (cap < sizeof(config_t))
        return 0;
    config_stamp(&g_config);
    memcpy(out, &g_config, sizeof(config_t));
    return sizeof(config_t);
}

bool config_deserialize(const uint8_t *in, size_t len)
{
    if (len != sizeof(config_t))
        return false;

    /* Copy first: `in` may be unaligned, and validating a clean object keeps
     * the check off the transport buffer. Static to stay off the small stack. */
    static config_t tmp;
    memcpy(&tmp, in, sizeof tmp);
    if (!config_is_valid(&tmp))
        return false;

    g_config = tmp;
    return true;
}
