/**
 * @file  keys.h
 * @brief 12 键扫描 + 消抖
 *
 * 掩码定义与实测串口协议完全一致：
 *   bit0  K1  UP      0x0001
 *   bit1  K2  LEFT    0x0002
 *   bit2  K3  DOWN    0x0004
 *   bit3  K4  RIGHT   0x0008
 *   bit4  K5  Y       0x0010
 *   bit5  K6  X       0x0020
 *   bit6  K7  A       0x0040
 *   bit7  K8  B       0x0080
 *   bit8  K9  SELECT  0x0100
 *   bit9  K10 START   0x0200
 *   bit10 K11 LT      0x0400
 *   bit11 K12 RT      0x0800
 */

#ifndef __KEYS_H
#define __KEYS_H

#include "CONFIG.h"

#define KEY_MASK_UP         0x0001u
#define KEY_MASK_LEFT       0x0002u
#define KEY_MASK_DOWN       0x0004u
#define KEY_MASK_RIGHT      0x0008u
#define KEY_MASK_Y          0x0010u
#define KEY_MASK_X          0x0020u
#define KEY_MASK_A          0x0040u
#define KEY_MASK_B          0x0080u
#define KEY_MASK_SELECT     0x0100u
#define KEY_MASK_START      0x0200u
#define KEY_MASK_LT         0x0400u
#define KEY_MASK_RT         0x0800u

#define KEY_MASK_ALL        0x0FFFu

void     keys_init(void);
uint16_t keys_scan_raw(void);

/**
 * @brief 采样一次并做消抖，建议每 5ms 调用一次。
 * @param  changed 输出：本次是否有按键状态变化（可为 NULL）
 * @return 消抖后的稳定掩码
 */
uint16_t keys_poll(uint8_t *changed);

#endif /* __KEYS_H */
