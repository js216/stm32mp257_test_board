// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file usb_msc.h
 * @brief USB mass-storage device exposing the SD card.
 * @copyright 2026 Jakob Kastelic
 */

#ifndef USB_MSC_H
#define USB_MSC_H

/** Init SD (if needed) + DWC3 and connect as a USB MSC device. 0 on ok. */
int usb_msc_start(void);

/** Service the USB controller; call continuously while exported. */
void usb_msc_poll(void);

/** Blocks moved so far (diagnostics for the console). */
extern unsigned long msc_read_count;
extern unsigned long msc_write_count;
extern unsigned long msc_setup_count;
extern unsigned long msc_init_count;

#endif // USB_MSC_H
