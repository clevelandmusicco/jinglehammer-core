#pragma once

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT    0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS         OPT_OS_NONE
#endif

#define CFG_TUD_ENABLED     1
#define CFG_TUD_MAX_SPEED   BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN  __attribute__ ((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif

#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            1
#define CFG_TUD_VENDOR          0

/* Roomier than the 64 B endpoint so a config frame (~1.7 KB blob, sent/received
 * in chunks across loop ticks) buffers without per-byte FIFO stalls. */
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256

#define CFG_TUD_MIDI_RX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_MIDI_TX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
