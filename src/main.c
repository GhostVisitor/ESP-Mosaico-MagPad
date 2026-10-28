/**
 * @file  main.c
 * @brief ESP-Mosaico 磁吸手柄：BLE HID + USB HID + 串口帧
 *
 * 数据通路：
 *   - 串口：状态变化时发送 A5 5A <lo> <hi> <xor>，始终输出；
 *   - USB ：枚举成功后优先走 USB HID（无 Report ID，7 字节负载）；
 *   - BLE ：USB 未连接时走 BLE HID 通知。
 *
 * 扫描周期 5ms，连续 3 次一致才更新状态（约 15ms 消抖）。
 * 按住按键期间每 100ms 重发一次当前报表，防止 BLE 通知丢失导致主机状态卡住。
 */

#include "CONFIG.h"
#include "HAL.h"
#include "hiddev.h"
#include "board.h"
#include "keys.h"
#include "pad_hid.h"
#include "pad_ble.h"
#include "uart_link.h"
#include "usb_hid.h"
#include "diag.h"
#include "battery.h"
#include "bas_service.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#define PAD_SCAN_PERIOD_MS      5
#define PAD_RESEND_TICKS        20 /* 20 * 5ms = 100ms */
#define PAD_UART_HB_TICKS       80 /* 80 * 5ms = 400ms，参考协议的串口心跳 */

#define PAD_EVT_SCAN            0x0001
#define PAD_EVT_RESTART_ADV     0x0002

/* SELECT + START 同时按住 3 秒：清除所有配对信息 */
#define PAD_ERASE_COMBO_TICKS   600

/* KEY9(SELECT) 单独按住 5 秒：PB14(LDO_EN) 拉低 -> 60ms -> 软件复位。
 * 电池供电时 LDO 关断即整机断电；USB 供电时 LDO 不断电，靠复位重启。 */
#ifndef PAD_POWEROFF_KEY_MASK
#define PAD_POWEROFF_KEY_MASK   KEY_MASK_SELECT
#endif
#ifndef PAD_POWEROFF_MS
#define PAD_POWEROFF_MS         5000u
#endif
#ifndef PAD_POWEROFF_DELAY_MS
#define PAD_POWEROFF_DELAY_MS   500u
#endif
#define PAD_POWEROFF_TICKS      (PAD_POWEROFF_MS / PAD_SCAN_PERIOD_MS)

/* 15 分钟无按键：PB14 拉低 + 进入 Shutdown，PB15 唤醒（唤醒后复位） */
#ifndef PAD_IDLE_MS
#define PAD_IDLE_MS             (15u * 60u * 1000u)
#endif
#define PAD_IDLE_TICKS          (PAD_IDLE_MS / PAD_SCAN_PERIOD_MS)

/* PB15 唤醒方式：本板 K10(PB15) 按下为高电平，内部下拉，因此默认
 * 用上升沿（按下瞬间）。若想按下/松开都唤醒，把 PAD_WAKE_ANY_EDGE 设为 1。 */
#ifndef PAD_WAKE_ANY_EDGE
#define PAD_WAKE_ANY_EDGE       0
#endif
#ifndef PAD_WAKE_EDGE
#define PAD_WAKE_EDGE           GPIO_ITMode_RiseEdge
#endif

/* 电池低压检测：内部 VBAT 通道，低于 3000mV 告警，10 秒后自动下电。
 * 每秒采样一次，连续 3 次低才确认；回升到 3050mV 以上则取消。 */
#ifndef PAD_BAT_ENABLE
#define PAD_BAT_ENABLE          1
#endif
#define PAD_BAT_LOW_MV          3000u
#define PAD_BAT_RECOVER_MV      3050u
#define PAD_BAT_CONFIRM         3
#define PAD_BAT_CHECK_TICKS     200     /* 200 * 5ms = 1s */
#define PAD_BAT_OFF_CHECKS      10      /* 10 次检查 = 10s */

/* BLE 电池服务只报两档：正常 100%，低压 5% */
#define PAD_BAS_LEVEL_OK        100
#define PAD_BAS_LEVEL_LOW       5

/* 红色运行指示灯（PB23）：正常常亮；电压低于 3.3V 时 0.25Hz 闪烁
 * （400 * 5ms = 2s 翻转一次 = 2s 亮 / 2s 灭） */
#ifndef PAD_LED_BLINK_MV
#define PAD_LED_BLINK_MV        3300u
#endif
#define PAD_LED_BLINK_HYST      50u
#define PAD_LED_BLINK_TICKS     400

static uint8_t  s_taskId;
static uint8_t  s_resendTicks;
static uint16_t s_hbTicks;
static uint16_t s_comboTicks;
static uint16_t s_poweroffTicks;
static uint32_t s_idleTicks;

static uint16_t s_batTicks;      /* 1s 采样节拍 */
static uint8_t  s_batLowCount;   /* 连续低电次数 */
static uint8_t  s_batLow;        /* 已进入低电告警 */
static uint8_t  s_batOffChecks;  /* 低电后经过的秒数 */
static uint16_t s_batMv;         /* 最近一次电压 */

static uint8_t  s_ledBlink;      /* 1 = 闪烁模式 */
static uint8_t  s_ledPhase;      /* 闪烁相位 */
static uint16_t s_ledTicks;      /* 闪烁计时 */

static void pad_send_hid(uint16_t mask)
{
    uint8_t report[PAD_REPORT_LEN];

    pad_hid_build(mask, report);

    if(usb_hid_ready())
    {
        usb_hid_send_report(report);
    }
    else
    {
        PadBle_SendReport(report);
    }
}

static void pad_apply(uint16_t mask)
{
    /* 串口只在状态变化时发帧，重复帧会被上位机当成重复事件 */
    uart_link_send_mask(mask);
    pad_send_hid(mask);
}

/**
 * @brief KEY9 长按关机：先上报全松开，再拉低 LDO_EN，最后软件复位。
 *        电池供电时拉低后 LDO 立刻断电，本函数执行不到复位；
 *        USB 供电时 LDO 不断电，等电平稳定后复位重启。
 */
static void pad_power_off(void)
{
    pad_send_hid(0);
    uart_link_send_mask(0);

    board_power_off(); /* PB14 = 0 */

    mDelaymS(PAD_POWEROFF_DELAY_MS); /* 等 LDO 输出电容放电；USB 供电时则是等待后复位 */

    SYS_ResetExecute();

    while(1)
    {
    }
}

/**
 * @brief 系统下电：PB14(LDO_EN) 拉低 -> 进入 Shutdown。
 *
 *        空闲关机（15 分钟无按键）和电池低压关机都走这里。
 *
 *        外部 EN 决定实际结果（硬件行为，固件不判断）：
 *        - 外部电源在线：PB14 拉低后 MCU 仍有电，进入 Shutdown，
 *          PB15 边沿唤醒并复位；
 *        - 外部电源不在线：PB14 拉低即切断整机供电，直接关机。
 */
static void pad_system_off(void)
{
    /* 1. 先把 PB15 配好为唤醒源（必须在断电之前完成）。
     *    K10 是高电平按下，配内部下拉，按下瞬间产生上升沿。 */
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PD);
    GPIOB_ITModeCfg(GPIO_Pin_15, PAD_WAKE_EDGE);
#if PAD_WAKE_ANY_EDGE
    GPIOB_ITEdgeTypeCfg(GPIO_Pin_15, ENABLE);
#endif
    GPIOB_ClearITFlagBit(GPIO_Pin_15);
    PFIC_EnableIRQ(GPIO_B_IRQn);
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE, Long_Delay);

    /* 2. 上报全松开，断开 USB（让主机看到设备离线，也省电） */
    pad_send_hid(0);
    uart_link_send_mask(0);
    USB_DisablePin();
    USB_Disable();

    /* 3. PB14 拉低；有外部电源时 MCU 靠它继续供电 */
    board_power_off();

    mDelaymS(50);

    /* 4. 进入 Shutdown；PB15 有边沿时唤醒并复位 */
    LowPower_Shutdown(0);

    /* 能执行到这里说明没睡下去（例如还有中断），退化为软复位重启 */
    SYS_ResetExecute();

    /* 唤醒后从复位向量重新运行，不会执行到这里 */
    while(1)
    {
    }
}

#if PAD_BAT_ENABLE
/**
 * @brief 电池巡检：每 5ms 调用，内部 1s 采样一次。
 *
 * 连续 3 次低于 3000mV 才确认告警，避免射频/负载瞬时压降误触发；
 * 告警后每秒重发一帧低电通知，10 秒后下电；中途回升到 3050mV 以上则取消。
 */
static void pad_battery_task(void)
{
    if(++s_batTicks < PAD_BAT_CHECK_TICKS)
    {
        return;
    }
    s_batTicks = 0;

    s_batMv = battery_read_mv();

    /* 指示灯：低于 3.3V 进入闪烁，回升到 3.35V 以上恢复常亮 */
    if(!s_ledBlink)
    {
        if(s_batMv < PAD_LED_BLINK_MV)
        {
            s_ledBlink = 1;
            s_ledTicks = 0;
            s_ledPhase = 0;
            board_led_set(0);
        }
    }
    else if(s_batMv >= (PAD_LED_BLINK_MV + PAD_LED_BLINK_HYST))
    {
        s_ledBlink = 0;
        s_ledTicks = 0;
        s_ledPhase = 0;
        board_led_set(1);
    }

    if(!s_batLow)
    {
        if(s_batMv < PAD_BAT_LOW_MV)
        {
            if(++s_batLowCount >= PAD_BAT_CONFIRM)
            {
                s_batLow = 1;
                s_batOffChecks = 0;
                uart_link_send_battery(s_batMv); /* 告警 */
                Bas_SetLevel(PAD_BAS_LEVEL_LOW); /* BLE 电池服务：5% */
            }
        }
        else
        {
            s_batLowCount = 0;
        }
        return;
    }

    /* 已经告警：电压回升则取消 */
    if(s_batMv >= PAD_BAT_RECOVER_MV)
    {
        s_batLow = 0;
        s_batLowCount = 0;
        Bas_SetLevel(PAD_BAS_LEVEL_OK); /* BLE 电池服务：恢复 100% */
        return;
    }

    /* 每秒重复一次告警帧 */
    uart_link_send_battery(s_batMv);

    if(++s_batOffChecks >= PAD_BAT_OFF_CHECKS)
    {
        pad_system_off(); /* 10 秒到，下电；不返回 */
    }
}
#endif

/**
 * @brief 运行指示灯：常亮，或 0.25Hz 闪烁（2s 亮 + 2s 灭）。每 5ms 调用。
 */
static void pad_led_task(void)
{
    if(!s_ledBlink)
    {
        return;
    }

    if(++s_ledTicks >= PAD_LED_BLINK_TICKS)
    {
        s_ledTicks = 0;
        s_ledPhase ^= 1;
        board_led_set(s_ledPhase);
    }
}

/**
 * @brief PB15 唤醒中断：只在进入 Shutdown 前可能被触发，清标志即可。
 */
__INTERRUPT
__HIGH_CODE
void GPIOB_IRQHandler(void)
{
    GPIOB_ClearITFlagBit(GPIO_Pin_15);
}

static uint16_t Pad_ProcessEvent(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(s_taskId)) != NULL)
        {
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & PAD_EVT_RESTART_ADV)
    {
        uint8_t enable = 1;
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &enable);
        return (events ^ PAD_EVT_RESTART_ADV);
    }

    if(events & PAD_EVT_SCAN)
    {
        uint8_t  changed = 0;
        uint16_t mask = keys_poll(&changed);

        /* KEY9(SELECT) 单独按住 5 秒：LDO_EN 拉低 + 重启 */
        if(mask == PAD_POWEROFF_KEY_MASK)
        {
            if(++s_poweroffTicks >= PAD_POWEROFF_TICKS)
            {
                pad_power_off(); /* 不返回 */
            }
        }
        else
        {
            s_poweroffTicks = 0;
        }

        /* 长按 SELECT+START 清除绑定，1 秒后重新开始广播 */
        if((mask & (KEY_MASK_SELECT | KEY_MASK_START)) == (KEY_MASK_SELECT | KEY_MASK_START))
        {
            if(s_comboTicks < PAD_ERASE_COMBO_TICKS)
            {
                s_comboTicks++;
                if(s_comboTicks == PAD_ERASE_COMBO_TICKS)
                {
                    HidDev_SetParameter(HIDDEV_ERASE_ALLBONDS, 0, NULL);
                    tmos_start_task(s_taskId, PAD_EVT_RESTART_ADV, 1000);
                }
            }
        }
        else
        {
            s_comboTicks = 0;
        }

        if(changed)
        {
            pad_apply(mask);
            s_resendTicks = 0;
            s_hbTicks = 0;
        }
        else if(mask != 0)
        {
            /* 按住不放时定期重发 HID 报表（不发串口），
             * 兼顾 BLE 通知丢失和“连接建立时按键已按下”的情况 */
            if(++s_resendTicks >= PAD_RESEND_TICKS)
            {
                s_resendTicks = 0;
                pad_send_hid(mask);
            }
        }
        else
        {
            s_resendTicks = 0;
        }

        /* 串口心跳：状态不变时也每 400ms 发一帧（参考协议要求） */
        if(++s_hbTicks >= PAD_UART_HB_TICKS)
        {
            s_hbTicks = 0;
            uart_link_send_mask(mask);
        }

        /* 电池低压巡检：低于 3.0V 告警，10 秒后下电 */
#if PAD_BAT_ENABLE
        pad_battery_task();
#endif

        /* 运行指示灯：常亮，或电压低于 3.3V 时 0.25Hz 闪烁 */
        pad_led_task();

        /* 15 分钟无按键：PB14 拉低 + Shutdown，PB15 唤醒 */
#if PAD_IDLE_MS > 0
        if(changed || mask != 0)
        {
            s_idleTicks = 0;
        }
        else if(++s_idleTicks >= PAD_IDLE_TICKS)
        {
            pad_system_off(); /* 不返回 */
        }
#endif

        return (events ^ PAD_EVT_SCAN);
    }

    return 0;
}

/* 主循环放在 RAM(.highcode) 中执行：BLE 射频活动期间取指 Flash 可能被拉长 */
__HIGH_CODE
__attribute__((noinline))
static void Main_Circulation(void)
{
    while(1)
    {
        TMOS_SystemProcess();
    }
}

#ifdef PAD_LDO_TEST
/* ------------------------------------------------------------------
 * 最小化 LDO 自保持测试固件（-DPAD_LDO_TEST）
 * 只做：关调试口 -> 拉高 PB14 -> 串口打印寄存器状态 -> 死循环。
 * 不初始化 BLE/USB/按键，用于区分“引脚没拉高”和“后续初始化/硬件掉电”。
 * ------------------------------------------------------------------ */
static void ldo_test_str(uint8_t *buf, uint8_t *n, const char *s)
{
    while(*s)
    {
        buf[(*n)++] = (uint8_t)*s++;
    }
}

static void ldo_test_hex(uint8_t *buf, uint8_t *n, uint8_t v)
{
    const char *h = "0123456789ABCDEF";

    buf[(*n)++] = h[v >> 4];
    buf[(*n)++] = h[v & 0x0F];
}

static void ldo_test_run(void)
{
    uint8_t buf[96];
    uint8_t n = 0;

    ldo_test_str(buf, &n, "\r\n[LDO] CFG=");
    ldo_test_hex(buf, &n, R8_GLOB_CFG_INFO);
    ldo_test_str(buf, &n, " DIR=");
    ldo_test_hex(buf, &n, (uint8_t)(R32_PB_DIR >> 8));
    ldo_test_str(buf, &n, " OUT=");
    ldo_test_hex(buf, &n, (uint8_t)(R32_PB_OUT >> 8));
    ldo_test_str(buf, &n, " PIN=");
    ldo_test_hex(buf, &n, (uint8_t)(R32_PB_PIN >> 8));
    ldo_test_str(buf, &n, " PU=");
    ldo_test_hex(buf, &n, (uint8_t)(R32_PB_PU >> 8));
    ldo_test_str(buf, &n, "\r\n");

    UART1_SendString(buf, n);

    while(1)
    {
        /* 保持 */
    }
}
#endif

int main(void)
{
    /* 最先保证供电自保持 */
    board_early_init();
    SetSysClock(CLK_SOURCE_PLL_60MHz);

#ifdef PAD_LDO_TEST
    uart_link_init();
    ldo_test_run(); /* 不返回 */
#endif

#ifdef PAD_DIAG_CDC
    diag_run(); /* 不返回 */
#endif

    keys_init();
    uart_link_init();
    battery_init();

    CH59x_BLEInit();
    HAL_Init();
    GAPRole_PeripheralInit();

    HidDev_Init();   /* GAP/GATT/设备信息服务 + 启动从机 */
    PadBle_Init();   /* 广播数据、配对参数、HID 服务 */

    /* USB 放在 BLE 之后初始化，与 WCH BLE_USB 例程顺序一致，
     * 避免 USB 枚举中断打断射频初始化 */
    usb_hid_init();

    /* 初始化过程中如果有什么操作动了 PB14，这里再兜底拉高一次 */
    board_hold_ldo();

    s_taskId = TMOS_ProcessEventRegister(Pad_ProcessEvent);
    tmos_start_reload_task(s_taskId, PAD_EVT_SCAN, PAD_SCAN_PERIOD_MS);

    /* 上电先上报一次全松开状态，便于主机/上位机同步 */
    pad_apply(0);

    Main_Circulation();
}
