/**
 * @file  pad_ble.h
 * @brief BLE HID 游戏手柄（从机）应用层
 */

#ifndef __PAD_BLE_H
#define __PAD_BLE_H

#include "CONFIG.h"
#include "pad_hid.h"

void PadBle_Init(void);

/* 发送一帧输入报表；未连接/未加密/未使能通知时静默丢弃 */
void PadBle_SendReport(const uint8_t payload[PAD_REPORT_LEN]);

#endif /* __PAD_BLE_H */
