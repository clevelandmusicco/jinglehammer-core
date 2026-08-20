/*
 * picotool reset interface: answers BOOTSEL/flash-reboot requests so
 * `picotool load -fx` works. Same contract as SDK's reset_interface.c but
 * custom here. VID/PID must be 0x2E8A/0x0009 (Raspberry Pi) for picotool
 * discovery.
 */

#include "tusb.h"
#include "device/usbd_pvt.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/usb_reset_interface.h"

/* Delay before reset to let host see clean status response. */
#define RESET_TO_FLASH_DELAY_MS 100u

static uint8_t itf_num;

static void resetd_init(void)
{
}

static void resetd_reset(uint8_t rhport)
{
    (void)rhport;
    itf_num = 0;
}

static uint16_t resetd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len)
{
    (void)rhport;

    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
              RESET_INTERFACE_SUBCLASS   == itf_desc->bInterfaceSubClass &&
              RESET_INTERFACE_PROTOCOL   == itf_desc->bInterfaceProtocol, 0);

    /* Interface descriptor only - this interface has no endpoints. */
    uint16_t const drv_len = sizeof(tusb_desc_interface_t);
    TU_VERIFY(max_len >= drv_len, 0);

    itf_num = itf_desc->bInterfaceNumber;
    return drv_len;
}

static bool resetd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;

    if (stage != CONTROL_STAGE_SETUP)
        return true; /* nothing to do on DATA/ACK */

    if (request->wIndex != itf_num)
        return false;

    switch (request->bRequest) {
    case RESET_REQUEST_BOOTSEL: {
        /* wValue bits: 6:0=bootrom mask, 7=LED polarity, 8=LED GPIO present,
         * 15:9=GPIO num. 0=bootrom default. */
        int  gpio       = (request->wValue & 0x100u) ? (int)(request->wValue >> 9u) : -1;
        bool active_low = (request->wValue & 0x080u) != 0;

        rom_reset_usb_boot_extra(gpio, request->wValue & 0x7fu, active_low);
        /* does not return */
    }
    case RESET_REQUEST_FLASH:
        watchdog_reboot(0, 0, RESET_TO_FLASH_DELAY_MS);
        return true;
    default:
        return false;
    }
}

static bool resetd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)rhport; (void)ep_addr; (void)result; (void)xferred_bytes;
    return true; /* no endpoints, so this never actually fires */
}

static usbd_class_driver_t const resetd_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "RESET",
#endif
    .init            = resetd_init,
    .reset           = resetd_reset,
    .open            = resetd_open,
    .control_xfer_cb = resetd_control_xfer_cb,
    .xfer_cb         = resetd_xfer_cb,
    .sof             = NULL,
};

/* Weak hook; app drivers tried first, so vendor interface claims this (CDC/MIDI ignore). */
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &resetd_driver;
}
