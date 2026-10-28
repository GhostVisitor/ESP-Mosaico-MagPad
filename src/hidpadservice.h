/**
 * @file  hidpadservice.h
 * @brief BLE HID 游戏手柄服务（HID over GATT，UUID 0x1812）
 */

#ifndef __HIDPADSERVICE_H
#define __HIDPADSERVICE_H

#include "CONFIG.h"

/* 服务中定义的报表数量（只有 1 个输入报表） */
#define HID_NUM_REPORTS     1

/* 输入报表 ID（与 pad_hid.h 保持一致） */
#define HID_RPT_ID_PAD_IN   1

/* HID Information 的 flags：远程唤醒 + 可连接 */
#define HID_FEATURE_FLAGS   (HID_FLAGS_REMOTE_WAKE | HID_FLAGS_NORMALLY_CONNECTABLE)

extern uint8_t  hidProtocolMode;
extern uint16_t hidReportMapLen;

bStatus_t HidPad_AddService(void);

uint8_t HidPad_SetParameter(uint8_t id, uint8_t type, uint16_t uuid, uint8_t len, void *pValue);
uint8_t HidPad_GetParameter(uint8_t id, uint8_t type, uint16_t uuid, uint16_t *pLen, void *pValue);

#endif /* __HIDPADSERVICE_H */
