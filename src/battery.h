/**
 * @file  battery.h
 * @brief 内部 VBAT 通道测电池电压（CH592 ADC 通道 14）
 */

#ifndef __BATTERY_H
#define __BATTERY_H

#include "CONFIG.h"

void     battery_init(void);

/**
 * @brief 读一次电池电压（mV）。
 *
 * CH592 的内部 VBAT 通道测的是芯片供电 VDD33。电池经 3.3V LDO 供电时：
 *   - 电池高于 3.3V + 压差：读到 3.3V（稳压值）
 *   - 电池跌到压差以下：VDD33 跟随电池下降，可反映电池电压
 */
uint16_t battery_read_mv(void);

#endif /* __BATTERY_H */
