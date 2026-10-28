/**
 * @file  diag.c
 * @brief USB CDC 诊断固件：证明板子正常启动，并输出 PB14/按键/配置状态
 */

#include "diag.h"
#include "board.h"
#include "keys.h"
#include "usb_cdc.h"
#include "battery.h"

#ifdef PAD_DIAG_CDC

static void diag_status(uint32_t alive, uint16_t mask)
{
    cdc_puts("[alive ");
    cdc_putu(alive);
    cdc_puts("] mask=");
    cdc_puthex16(mask);
    cdc_puts(" PB14=");
    cdc_puts((R32_PB_PIN & BOARD_LDO_EN_PIN) ? "H" : "L");
    cdc_puts(" CFG=");
    cdc_puthex8(R8_GLOB_CFG_INFO);
    cdc_puts(" DIR=");
    cdc_puthex8((uint8_t)(R32_PB_DIR >> 8));
    cdc_puts(" OUT=");
    cdc_puthex8((uint8_t)(R32_PB_OUT >> 8));
    cdc_puts(" PIN=");
    cdc_puthex8((uint8_t)(R32_PB_PIN >> 8));
    cdc_puts(" BAT=");
    cdc_putu(battery_read_mv());
    cdc_puts("\r\n");
}

void diag_run(void)
{
    uint32_t tick = 0;
    uint32_t alive = 0;
    uint16_t lastMask;

    keys_init();
    cdc_init();
    battery_init();

    cdc_puts("\r\n=== CH592 PAD DIAG (CDC) ===\r\n");
    cdc_puts("build " __DATE__ " " __TIME__ "\r\n");
    cdc_puts("CFG bit4 must be 0 (debug port disabled)\r\n");
    cdc_puts("DIR/OUT/PIN bit6 (0x40) = PB14\r\n");

    lastMask = keys_poll(0);
    diag_status(alive, lastMask);

    while(1)
    {
        uint8_t  changed = 0;
        uint16_t mask = keys_poll(&changed);

        cdc_task();

        if(mask != lastMask)
        {
            lastMask = mask;
            cdc_puts("[key] mask=");
            cdc_puthex16(mask);
            cdc_puts("\r\n");
        }

        mDelaymS(10);

        if(++tick >= 50) /* 约 500ms */
        {
            tick = 0;
            diag_status(++alive, lastMask);
        }
    }
}

#endif /* PAD_DIAG_CDC */
