/**
 * @file  pad_hid.h
 * @brief USB / BLE 共用 HID 报表定义（与 ESP-Mosaico CH552T 版一致）
 *
 * 输入报表（USB 上无 Report ID，共 7 字节；BLE 带 Report ID = 1）：
 *   byte0 : X   左摇杆 X（0..255，128 中位）
 *   byte1 : Y   左摇杆 Y
 *   byte2 : Rx  右摇杆 X（固定 128）
 *   byte3 : Ry  右摇杆 Y（固定 128）
 *   byte4 : hat + 4 位填充（0=上 2=右 4=下 6=左 8=中位）
 *   byte5 : 按钮 1..8  A B X Y LB RB LT RT
 *   byte6 : 按钮 9..10 Back Start（其余位 0）
 *
 * 按钮定义（W3C 标准手柄布局，与参考固件完全一致）：
 *   Button 1  A      <- K7  (PB4)
 *   Button 2  B      <- K8  (PB7)
 *   Button 3  X      <- K6  (PA15)
 *   Button 4  Y      <- K5  (PA14)
 *   Button 5  LB     <- 无
 *   Button 6  RB     <- 无
 *   Button 7  LT     <- K11 (PB12)
 *   Button 8  RT     <- K12 (PB13)
 *   Button 9  Back   <- K9  (PB22, SELECT)
 *   Button 10 Start  <- K10 (PB15, START)
 */

#ifndef __PAD_HID_H
#define __PAD_HID_H

#include "CONFIG.h"

#define PAD_REPORT_ID       1       /* 仅 BLE 使用 */
#define PAD_REPORT_LEN      7

/* D-Pad 同时驱动左摇杆满偏（参考固件默认行为），置 0 可关闭 */
#ifndef PAD_DPAD_DRIVES_STICK
#define PAD_DPAD_DRIVES_STICK   1
#endif

/* Y 轴方向：默认按 DirectInput 惯例（上 = 0，下 = 255）。
 * 若实际方向相反，把下面这个宏改成 1。 */
#ifndef PAD_Y_AXIS_INVERTED
#define PAD_Y_AXIS_INVERTED     0
#endif

#define PAD_HAT_UP          0
#define PAD_HAT_UP_RIGHT    1
#define PAD_HAT_RIGHT       2
#define PAD_HAT_DOWN_RIGHT  3
#define PAD_HAT_DOWN        4
#define PAD_HAT_DOWN_LEFT   5
#define PAD_HAT_LEFT        6
#define PAD_HAT_UP_LEFT     7
#define PAD_HAT_NEUTRAL     8

/* 由按键掩码生成 7 字节报表负载 */
void pad_hid_build(uint16_t mask, uint8_t out[PAD_REPORT_LEN]);

/* 由方向键计算 hat 值 */
uint8_t pad_hid_hat(uint16_t mask);

/* USB 报表描述符不带 Report ID；BLE 的带 Report ID = 1 */
extern const uint8_t  pad_hid_report_map_usb[];
extern const uint8_t  pad_hid_report_map_ble[];
extern const uint16_t pad_hid_report_map_usb_len;
extern const uint16_t pad_hid_report_map_ble_len;

#endif /* __PAD_HID_H */
