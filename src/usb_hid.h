/**
 * @file  usb_hid.h
 * @brief USB HID 游戏手柄接口
 */

#ifndef __USB_HID_H
#define __USB_HID_H

#include "CONFIG.h"
#include "pad_hid.h"

void    usb_hid_init(void);
uint8_t usb_hid_ready(void);
void    usb_hid_send_report(const uint8_t payload[PAD_REPORT_LEN]);

#endif /* __USB_HID_H */
