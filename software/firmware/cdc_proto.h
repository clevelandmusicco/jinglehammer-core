#pragma once

/*
 * USB-CDC config protocol: the web app's read/write/save link to g_config.
 *
 * A small framed command set on the CDC data interface. The wire payload for
 * READ/WRITE is the raw little-endian config_t blob - identical to the flash
 * layout - so the browser mirrors config.h byte-for-byte and no SysEx-style
 * 7-bit re-encoding is needed (see config-transport decision).
 *
 * Frames (host->device):  'M' 'C' | cmd(1) | len(2 LE) | payload[len]
 * Frames (device->host):  'M' 'C' | cmd(1) | status(1) | len(2 LE) | payload[len]
 *
 * No per-frame CRC: USB bulk already guarantees link integrity, and the blob
 * carries its own crc32 end-to-end (checked by config_deserialize on WRITE).
 *
 * Call cdc_proto_task() from the main super-loop, right after tud_task().
 */
void cdc_proto_task(void);
