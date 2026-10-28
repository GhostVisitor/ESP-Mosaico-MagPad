/**
 * @file  bas_service.h
 * @brief 标准蓝牙电池服务（Battery Service, UUID 0x180F）
 *
 * 只提供标准的 Battery Level 特征（0x2A19，0..100，Read + Notify），
 * 主机（Windows/手机）可以直接显示电量图标。
 */

#ifndef __BAS_SERVICE_H
#define __BAS_SERVICE_H

#include "CONFIG.h"

void    Bas_AddService(void);

/** 设置电量百分比（0..100），变化时向已订阅的主机发通知 */
void    Bas_SetLevel(uint8_t level);

uint8_t Bas_GetLevel(void);

#endif /* __BAS_SERVICE_H */
