/*
 * picotool's reset interface: a zero-endpoint vendor interface whose whole job
 * is answering two control requests, "reboot into BOOTSEL" and "reboot into
 * flash". That is what lets `picotool load -fx` reset a running board over USB
 * instead of somebody unplugging the controller and holding BOOTSEL.
 *
 * Same wire contract as pico_stdio_usb's reset interface (SDK
 * src/rp2_common/pico_stdio_usb/reset_interface.c), re-done here because this
 * firmware brings its own descriptors and does not link pico_stdio_usb.
 * TinyUSB picks the driver up through the weak usbd_app_driver_get_cb() hook,
 * so CFG_TUD_VENDOR stays 0 - no vendor class driver, no endpoints, no FIFOs.
 *
 * Note the device VID/PID (usb_descriptors.c) has to stay 0x2E8A/0x0009 for
 * this to be reachable: picotool only looks for the reset interface on
 * Raspberry Pi VID devices unless you pass --vid 0, and picotool's udev rules
 * only grant access to those same IDs.
 */

#include "tusb.h"
#include "device/usbd_pvt.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/usb_reset_interface.h"

/* Give the control transfer time to finish its status stage before the
 * watchdog yanks the chip, so the host sees a clean answer rather than a
 * mid-transfer disconnect. */
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
        /*
         * wValue layout, as picotool packs it: bits 6:0 = bootrom interface
         * disable mask, bit 7 = activity LED is active-low, bit 8 = an
         * activity-LED GPIO follows, bits 15:9 = that GPIO number. Only set
         * when the caller passes picotool's --led/-a; the plain `-f` path
         * sends 0 and the bootrom picks its own default.
         */
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

/* Weak hook in TinyUSB's usbd.c: app drivers are tried before the built-in
 * class drivers, so this claims the vendor interface CDC and MIDI ignore. */
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &resetd_driver;
}
