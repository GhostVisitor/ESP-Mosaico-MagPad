/**
 * @file  battery.c
 * @brief 内部 VBAT 通道测电池电压
 *
 * 硬件：单节锂电池 -> 3.3V LDO -> CH592F VDD33。
 * CH592 内部电池检测通道（ADC 通道 14）测的是 VDD33，配合 -12dB PGA
 * （1/4 衰减）可以覆盖 5.25V 满量程，正好用于 4.2V 锂电池。
 *
 * 换算（数据手册第 15 章，单端输入 -12dB）：
 *     VIN = (ADC / 512 - 3) * Vref ,  Vref ≈ 1.05V
 * 即：
 *     VIN(mV) = (ADC - 1536) * 1050 / 512
 *
 * 注意：Vref 为芯片内部 1.05V（±1.5%），因此绝对值有几十 mV 的误差，
 * 用于 3.0V 低压判断足够；需要精确值请用万用表实测后修正 BAT_VREF_MV。
 */

#include "battery.h"

#define BAT_VREF_MV     1050    /* VINTA 参考电压，可实测修正 */
#define BAT_ADC_ZERO    1536    /* 3 * 512 */
#define BAT_SAMPLES     8

static signed short s_calib;

void battery_init(void)
{
    ADC_InterBATSampInit();
    s_calib = ADC_DataCalib_Rough(); /* 内部偏置校准值 */
    ADC_DisablePower();
}

uint16_t battery_read_mv(void)
{
    uint32_t sum = 0;
    uint8_t  i;
    int32_t  mv;

    ADC_InterBATSampInit();

    /* 丢掉前两次，等输入稳定 */
    (void)ADC_ExcutSingleConver();
    (void)ADC_ExcutSingleConver();

    for(i = 0; i < BAT_SAMPLES; i++)
    {
        sum += ADC_ExcutSingleConver();
    }

    ADC_DisablePower();

    mv = (int32_t)(sum / BAT_SAMPLES) + s_calib;
    if(mv < 0)
    {
        mv = 0;
    }

    mv = (mv - BAT_ADC_ZERO) * BAT_VREF_MV / 512;

    if(mv < 0)
    {
        mv = 0;
    }
    if(mv > 6000)
    {
        mv = 6000;
    }

    return (uint16_t)mv;
}
