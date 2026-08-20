#include <string.h>
#include "bsp/board_api.h"
#include "tusb.h"

#include "pico/usb_reset_interface.h"

/*
 * Raspberry Pi's VID paired with the SDK's own RP2350 CDC PID. Not vanity:
 * picotool only hunts for the reset interface (usb_reset_iface.c) on 0x2E8A
 * devices unless you hand it --vid 0, and the udev rules picotool ships grant
 * access to exactly these IDs. A private VID here would cost a hand-written
 * udev rule plus a flag on every picotool invocation.
 */
#define USB_VID  0x2E8Au
#define USB_PID  0x0009u

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    /* 0x0210, not 0x0200: Windows only asks for the BOS descriptor (and with
     * it the MS OS 2.0 set that auto-binds WinUSB to the reset interface) on
     * devices claiming USB 2.1. */
    .bcdUSB             = 0x0210,
    /* 0xEF/0x02/0x01 required for Windows to honour the IAD that CDC uses. */
    .bDeviceClass       = 0xEF,
    .bDeviceSubClass    = 0x02,
    .bDeviceProtocol    = 0x01,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    /* Windows caches "does this device have MS OS descriptors" under
     * HKLM\SYSTEM\CurrentControlSet\Control\usbflags, keyed on VID+PID+
     * bcdDevice, and never re-asks for a key it has seen. Bumped once when the
     * MS OS 2.0 set was added so machines that met an earlier build (or some
     * other 2e8a:0009 device) re-read rather than trusting a stale "no". */
    .bcdDevice          = 0x0101,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_RESET, /* picotool reset interface: control requests only, no endpoints */
    ITF_NUM_TOTAL,
};

#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82
#define EPNUM_MIDI_OUT   0x03
#define EPNUM_MIDI_IN    0x83

/* Bare interface descriptor, no endpoints - same shape pico_stdio_usb emits. */
#define TUD_RPI_RESET_DESC_LEN 9
#define TUD_RPI_RESET_DESCRIPTOR(_itfnum, _stridx) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, \
    RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, _stridx,

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_RPI_RESET_DESC_LEN)

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 0, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
    TUD_RPI_RESET_DESCRIPTOR(ITF_NUM_RESET, 0x04) /* macro carries its own trailing comma */
};

/* wTotalLength in the config descriptor is hand-summed above; a mismatch makes
 * the host truncate or over-read the descriptor set, which is a miserable bug
 * to chase from the enumeration end. */
TU_VERIFY_STATIC(sizeof(desc_fs_configuration) == CONFIG_TOTAL_LEN, "config descriptor length mismatch");

/*
 * Microsoft OS 2.0 descriptors. Windows 8.1+ reads these and binds winusb.sys
 * to the reset interface on its own, which is what picotool needs to talk to
 * it; without them a Windows user has to run Zadig and hand-pick interface 4,
 * and picking the wrong one there swaps the CDC's driver out and takes the
 * config editor down with it. Other hosts ignore all of this: Linux and macOS
 * hand an unclaimed vendor interface to libusb without being asked.
 *
 * Lifted from the SDK's pico_stdio_usb (stdio_usb_descriptors.c /
 * reset_interface.c), including its DeviceInterfaceGUID, with the function
 * subset pointed at our own reset interface number. Byte-counted by hand, so
 * the static asserts below matter.
 */
#define MS_OS_20_DESC_LEN 166u

#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

/* Vendor request code the host uses to fetch the set; arbitrary, but it lands
 * in bRequest, so it must match the check in tud_vendor_control_xfer_cb(). */
#define VENDOR_REQUEST_MICROSOFT 0x01

uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

TU_VERIFY_STATIC(sizeof(desc_bos) == BOS_TOTAL_LEN, "BOS descriptor length mismatch");

static uint8_t const desc_ms_os_20[] = {
    /* Set header: length, type, windows version, total length */
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    /* Function subset header: length, type, first interface, reserved, subset
     * length. Scoping this to ITF_NUM_RESET is what keeps WinUSB off the CDC
     * and MIDI interfaces, which keep their own class drivers. */
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_NUM_RESET, 0, U16_TO_U8S_LE(0x009C),

    /* Compatible ID: length, type, compatible ID, sub compatible ID */
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Registry property: DeviceInterfaceGUID, so WinUSB exposes the interface
     * to libusb (and through it picotool). */
    U16_TO_U8S_LE(0x0080), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0001), U16_TO_U8S_LE(0x0028), /* type = UTF-16 sz, name length */
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00,
    't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00,
    'U', 0x00, 'I', 0x00, 'D', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x004E), /* wPropertyDataLength */
    /* {bc7398c1-73cd-4cb7-98b8-913a8fca7bf6} */
    '{', 0, 'b', 0, 'c', 0, '7', 0, '3', 0, '9', 0,
    '8', 0, 'c', 0, '1', 0, '-', 0, '7', 0, '3', 0,
    'c', 0, 'd', 0, '-', 0, '4', 0, 'c', 0, 'b', 0,
    '7', 0, '-', 0, '9', 0, '8', 0, 'b', 0, '8', 0,
    '-', 0, '9', 0, '1', 0, '3', 0, 'a', 0, '8', 0,
    'f', 0, 'c', 0, 'a', 0, '7', 0, 'b', 0, 'f', 0,
    '6', 0, '}', 0, 0, 0
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "MS OS 2.0 descriptor length mismatch");

static char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 }, /* 0: LangID = English */
    "Cleveland Music Co.",        /* 1: manufacturer */
    "Jinglehammer Lite",          /* 2: product */
    NULL,                         /* 3: serial, filled from board unique ID */
    "Reset",                      /* 4: picotool reset interface */
};

static uint16_t desc_str[33];

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_fs_configuration;
}

uint8_t const *tud_descriptor_bos_cb(void)
{
    return desc_bos;
}

/*
 * Device-level vendor request, so it never reaches the reset interface's own
 * driver (usb_reset_iface.c) - TinyUSB routes it to this weak callback even
 * with CFG_TUD_VENDOR 0. wIndex 7 is MS OS 2.0's "get descriptor set".
 */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    if (request->bRequest == VENDOR_REQUEST_MICROSOFT && request->wIndex == 7)
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, sizeof(desc_ms_os_20));

    return false; /* stall anything else */
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t chr_count;

    switch (index) {
    case 0:
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
        break;
    case 3:
        chr_count = board_usb_get_serial(desc_str + 1, 32);
        break;
    default:
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
            return NULL;
        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 32)
            chr_count = 32;
        for (size_t i = 0; i < chr_count; i++)
            desc_str[1 + i] = str[i];
        break;
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
