/**
 * @file  usb_cdc.h
 * @brief USB CDC（虚拟串口）调试输出
 */

#ifndef __USB_CDC_H
#define __USB_CDC_H

#include "CONFIG.h"

void cdc_init(void);
void cdc_task(void);

void cdc_write(const uint8_t *data, uint16_t len);
void cdc_puts(const char *s);
void cdc_puthex8(uint8_t v);
void cdc_puthex16(uint16_t v);
void cdc_putu(uint32_t v);

#endif /* __USB_CDC_H */
