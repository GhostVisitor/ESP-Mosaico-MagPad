/**
 * @file  board.h
 * @brief CH592F（QFN28）三模手柄板级定义
 *
 * 引脚来自实际原理图，已逐一对照 CH592/CH591 数据手册 V1.9 的 CH592F 列核对：
 *
 *   1  PA11 / X32KO        2  PA10 / X32KI        3  VDCID
 *   4  VSW                 5  VDD33 / VIO33       6  PA8  / RXD1
 *   7  PA9  / TXD1         8  PB15                9  PB14
 *  10  PB13               11  PB12               12  PB11 / UD+
 *  13  PB10 / UD-         14  PB7                15  PB4
 *  16  PB23               17  PB22               18  X32MO
 *  19  X32MI              20  VINTA              21  ANT
 *  22  VDCIA              23  PA4                24  PA5
 *  25  PA15               26  PA14               27  PA13               28  PA12
 *
 * 注意：串口在丝印上写作 U0RXD/U0TXD，但物理上接的是 6/7 脚，即
 *       PA8(RXD1) / PA9(TXD1)，所以固件使用 **UART1**，不是 UART0。
 *       PB4/PB7（UART0 的默认引脚）已被按键 K7/K8 占用。
 */

#ifndef __BOARD_H
#define __BOARD_H

#include "CONFIG.h"

/* ------------------------------------------------------------------ LDO */
/* PB14 = LDO_EN，上电后必须尽快拉高，让 LDO 自保持供电 */
#define BOARD_LDO_EN_PIN        GPIO_Pin_14

/* --------------------------------------------------------------- PB23 */
/* PB23 = 红色运行指示灯：高电平点亮，低电平熄灭。
 * 复位后 CFG_RESET_EN 默认为 0，PB23 是普通 GPIO，可直接推挽输出。
 * 注意：若选项字节里打开了 RST 功能，PB23 会被复位脚占用，LED 无法控制。 */
#define BOARD_LED_PIN           GPIO_Pin_23
#define BOARD_LED_ON_LEVEL      1 /* 1 = 高电平点亮 */

/* --------------------------------------------------------------- UART1 */
/* 6 脚 = PA8 = RXD1，7 脚 = PA9 = TXD1
 * 参考协议（ESP-Mosaico PROTOCOL.md）：19200 8N1 */
#define BOARD_UART_TX_PIN       GPIO_Pin_9
#define BOARD_UART_RX_PIN       GPIO_Pin_8
#define BOARD_UART_BAUDRATE     19200u

/* ---------------------------------------------------------------- USB */
/* 12 脚 = PB11 = UD+，13 脚 = PB10 = UD-（USB_DeviceInit 内部处理） */

/* ---------------------------------------------------------------- 按键 */
#define KEY_COUNT               12

/* 端口编号 */
#define KEY_PORT_A              0
#define KEY_PORT_B              1

typedef struct
{
    uint8_t  port;         /* KEY_PORT_A / KEY_PORT_B */
    uint32_t pin;          /* GPIO_Pin_x */
    uint16_t mask;         /* 上报掩码，见 keys.h */
    uint8_t  active_high;  /* 0=按下接地(低有效)，1=按下接高(高有效) */
} board_key_t;

/* 上电最早期初始化：LDO 自保持、关闭调试口、PB23 高阻 */
void board_early_init(void);

/* 重新把 PB14(LDO_EN) 拉高（可在时钟切换等操作后再调用一次） */
void board_hold_ldo(void);

/* 把 PB14(LDO_EN) 拉低：关断 LDO（电池供电时整机断电；USB 供电时仅
 * 拉低电平，需要配合软件复位）。 */
void board_power_off(void);

/* 运行指示灯（PB23）开关：on != 0 点亮 */
void board_led_set(uint8_t on);

/* 按键表：索引 0..11 依次对应 K1..K12 / mask bit0..bit11
 * 默认按下接地（低有效，内部上拉）；K10(PB15) 特殊：按下为高电平，
 * 使用内部下拉。 */
#define BOARD_KEY_LIST                                                                                     \
    {                                                                                                      \
        {KEY_PORT_A, GPIO_Pin_4,  0x0001, 0},  /* K1  PA4  23 脚  UP      */                               \
        {KEY_PORT_A, GPIO_Pin_5,  0x0002, 0},  /* K2  PA5  24 脚  LEFT    */                               \
        {KEY_PORT_A, GPIO_Pin_12, 0x0004, 0},  /* K3  PA12 28 脚  DOWN    */                               \
        {KEY_PORT_A, GPIO_Pin_13, 0x0008, 0},  /* K4  PA13 27 脚  RIGHT   */                               \
        {KEY_PORT_A, GPIO_Pin_14, 0x0010, 0},  /* K5  PA14 26 脚  Y       */                               \
        {KEY_PORT_A, GPIO_Pin_15, 0x0020, 0},  /* K6  PA15 25 脚  X       */                               \
        {KEY_PORT_B, GPIO_Pin_4,  0x0040, 0},  /* K7  PB4  15 脚  A       */                               \
        {KEY_PORT_B, GPIO_Pin_7,  0x0080, 0},  /* K8  PB7  14 脚  B       */                               \
        {KEY_PORT_B, GPIO_Pin_22, 0x0100, 0},  /* K9  PB22 17 脚  SELECT  */                               \
        {KEY_PORT_B, GPIO_Pin_15, 0x0200, 1},  /* K10 PB15  8 脚  START（高电平按下）*/                    \
        {KEY_PORT_B, GPIO_Pin_12, 0x0400, 0},  /* K11 PB12 11 脚  LT      */                               \
        {KEY_PORT_B, GPIO_Pin_13, 0x0800, 0},  /* K12 PB13 10 脚  RT      */                               \
    }

#endif /* __BOARD_H */