/**
 * @file  pad_hid.c
 * @brief HID 报表描述符与按键掩码到报表的转换
 *
 * 报表布局与按键顺序与 ESP-Mosaico（CH552T 版）固件一致：
 *   7 字节 = X, Y, Rx, Ry, hat+pad, btnLo, btnHi
 */

#include "pad_hid.h"
#include "keys.h"

/* ---------------------------------------------------------------- 描述符 */

#define PAD_HID_COLLECTION_START \
    0x05, 0x01,       /* Usage Page (Generic Desktop) */ \
    0x09, 0x05,       /* Usage (Game Pad)             */ \
    0xA1, 0x01        /* Collection (Application)     */

#define PAD_HID_REPORT_BODY                                                           \
    /* X / Y / Rx / Ry，0..255，128 中位 */                                           \
    0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34,                                   \
    0x15, 0x00, 0x26, 0xFF, 0x00,                                                     \
    0x75, 0x08, 0x95, 0x04, 0x81, 0x02,                                               \
    /* Hat switch：0..7，Null State，单位度 */                                        \
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14,     \
    0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,                                   \
    0x75, 0x04, 0x95, 0x01, 0x81, 0x01,                                               \
    /* 按钮 1..10：A B X Y LB RB LT RT Back Start */                                  \
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0A, 0x15, 0x00, 0x25, 0x01,                       \
    0x75, 0x01, 0x95, 0x0A, 0x81, 0x02,                                               \
    0x75, 0x06, 0x95, 0x01, 0x81, 0x01,                                               \
    0xC0              /* End Collection */

const uint8_t pad_hid_report_map_usb[] = {
    PAD_HID_COLLECTION_START,
    PAD_HID_REPORT_BODY
};

const uint8_t pad_hid_report_map_ble[] = {
    PAD_HID_COLLECTION_START,
    0x85, PAD_REPORT_ID, /* Report ID */
    PAD_HID_REPORT_BODY
};

const uint16_t pad_hid_report_map_usb_len = sizeof(pad_hid_report_map_usb);
const uint16_t pad_hid_report_map_ble_len = sizeof(pad_hid_report_map_ble);

/* ---------------------------------------------------------------- 报表 */

uint8_t pad_hid_hat(uint16_t mask)
{
    int8_t dx = 0;
    int8_t dy = 0;

    if(mask & KEY_MASK_RIGHT)
    {
        dx++;
    }
    if(mask & KEY_MASK_LEFT)
    {
        dx--;
    }
    if(mask & KEY_MASK_UP)
    {
        dy++; /* 屏幕坐标：上为 +1 */
    }
    if(mask & KEY_MASK_DOWN)
    {
        dy--;
    }

    if(dx == 0 && dy == 0)
    {
        return PAD_HAT_NEUTRAL;
    }
    if(dy > 0)
    {
        return (dx > 0) ? PAD_HAT_UP_RIGHT : ((dx < 0) ? PAD_HAT_UP_LEFT : PAD_HAT_UP);
    }
    if(dy < 0)
    {
        return (dx > 0) ? PAD_HAT_DOWN_RIGHT : ((dx < 0) ? PAD_HAT_DOWN_LEFT : PAD_HAT_DOWN);
    }
    return (dx > 0) ? PAD_HAT_RIGHT : PAD_HAT_LEFT;
}

void pad_hid_build(uint16_t mask, uint8_t out[PAD_REPORT_LEN])
{
    uint8_t btnLo = 0;
    uint8_t btnHi = 0;
    uint8_t x = 128;
    uint8_t y = 128;

    /* 按钮定义严格按参考固件的 W3C 顺序 */
    if(mask & KEY_MASK_A)
    {
        btnLo |= 1u << 0; /* Button 1  A     <- K7  */
    }
    if(mask & KEY_MASK_B)
    {
        btnLo |= 1u << 1; /* Button 2  B     <- K8  */
    }
    if(mask & KEY_MASK_X)
    {
        btnLo |= 1u << 2; /* Button 3  X     <- K6  */
    }
    if(mask & KEY_MASK_Y)
    {
        btnLo |= 1u << 3; /* Button 4  Y     <- K5  */
    }
    /* Button 5 LB / Button 6 RB：本板没有 */
    if(mask & KEY_MASK_LT)
    {
        btnLo |= 1u << 6; /* Button 7  LT    <- K11 */
    }
    if(mask & KEY_MASK_RT)
    {
        btnLo |= 1u << 7; /* Button 8  RT    <- K12 */
    }
    if(mask & KEY_MASK_SELECT)
    {
        btnHi |= 1u << 0; /* Button 9  Back  <- K9  */
    }
    if(mask & KEY_MASK_START)
    {
        btnHi |= 1u << 1; /* Button 10 Start <- K10 */
    }

    /* 参考固件：D-Pad 同时把左摇杆推到满偏 */
#if PAD_DPAD_DRIVES_STICK
    if(mask & KEY_MASK_LEFT)
    {
        x = 0;
    }
    if(mask & KEY_MASK_RIGHT)
    {
        x = 255;
    }
#if PAD_Y_AXIS_INVERTED
    if(mask & KEY_MASK_UP)
    {
        y = 255;
    }
    if(mask & KEY_MASK_DOWN)
    {
        y = 0;
    }
#else
    if(mask & KEY_MASK_UP)
    {
        y = 0;
    }
    if(mask & KEY_MASK_DOWN)
    {
        y = 255;
    }
#endif
#endif

    out[0] = x;
    out[1] = y;
    out[2] = 128; /* Rx：右摇杆固定中位 */
    out[3] = 128; /* Ry */
    out[4] = (uint8_t)(pad_hid_hat(mask) & 0x0F);
    out[5] = btnLo;
    out[6] = btnHi;
}
