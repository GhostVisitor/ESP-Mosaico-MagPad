/**
 * @file  board.c
 * @brief 板级最早期的初始化：LDO 自保持、关闭调试口、PB23 高阻
 */

#include "board.h"

/**
 * @brief 驱动 LDO_EN 为高（PB14），让 LDO 自保持。
 *
 * 注意顺序：先写输出锁存，再切输出方向，避免切换瞬间输出 0 造成掉电。
 * 用 20mA 档驱动，EN 线上即使有较大对地电容/下拉也能快速拉高。
 */
void board_hold_ldo(void)
{
    GPIOB_SetBits(BOARD_LDO_EN_PIN);
    GPIOB_ModeCfg(BOARD_LDO_EN_PIN, GPIO_ModeOut_PP_20mA);
    GPIOB_SetBits(BOARD_LDO_EN_PIN); /* 再写一次锁存，确保为 1 */
}

void board_power_off(void)
{
    /* 拉低前先确保是推挽输出，避免变成高阻后被外部上拉 */
    GPIOB_ModeCfg(BOARD_LDO_EN_PIN, GPIO_ModeOut_PP_20mA);
    GPIOB_ResetBits(BOARD_LDO_EN_PIN);
    GPIOB_ResetBits(BOARD_LDO_EN_PIN);
}

/**
 * @brief 运行指示灯（PB23）：高电平点亮。
 */
void board_led_set(uint8_t on)
{
#if BOARD_LED_ON_LEVEL
    if(on)
    {
        GPIOB_SetBits(BOARD_LED_PIN);
    }
    else
    {
        GPIOB_ResetBits(BOARD_LED_PIN);
    }
#else
    if(on)
    {
        GPIOB_ResetBits(BOARD_LED_PIN);
    }
    else
    {
        GPIOB_SetBits(BOARD_LED_PIN);
    }
#endif
}

void board_early_init(void)
{
    /* 1. 关闭两线仿真调试接口。
     *    CH592 复位后 CFG_DEBUG_EN 默认为 1，此时 PB14/PB15 只能作为
     *    TIO/TCK，不能当 GPIO 用（见数据手册 1.2 节引脚表注 3）。
     *    本板 PB14 是 LDO_EN，必须在驱动它之前先关掉调试接口。
     *    量产时建议用 ISP 工具把 CFG_DEBUG_EN 直接配置为 0。 */
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
    R16_PIN_ALTERNATE |= RB_DEBUG_EN;
    R8_SAFE_ACCESS_SIG = 0;

    /* 2. LDO 自保持 */
    board_hold_ldo();

    /* 3. PB23 运行指示灯：推挽输出，默认点亮表示已上电运行 */
    GPIOB_ModeCfg(BOARD_LED_PIN, GPIO_ModeOut_PP_5mA);
    board_led_set(1);
}
