/**
 * @file  usb_cdc.c
 * @brief USB CDC-ACM 虚拟串口（仅用于 PAD_DIAG_CDC 诊断固件）
 *
 * 端点分配：
 *   EP2 IN  : CDC 通知（中断，占位，不发数据）
 *   EP3 IN  : CDC 数据上传（批量 64 字节）
 *   EP3 OUT : CDC 数据下传（批量，丢弃）
 *
 * 发送采用 512 字节环形缓冲，主循环调用 cdc_task() 按 64 字节分片发送；
 * 主机没打开串口/没取数据时不会阻塞固件。
 */

#include "usb_cdc.h"
#include "CH59x_usbdev.h"

#ifdef PAD_DIAG_CDC

#define DevEP0SIZE      0x40
#define CDC_EP_NOTIFY   2
#define CDC_EP_DATA     3

/* ------------------------------------------------------------------ 描述符 */

/* 设备描述符：CDC 类，VID 0x1A86 / PID 0xE1A8（与 HID 固件区分） */
static const uint8_t MyDevDescr[] = {
    0x12, 0x01, 0x00, 0x02, 0x02, 0x00, 0x00, DevEP0SIZE,
    0x86, 0x1A, 0xA8, 0xE1, 0x00, 0x01,
    0x01, 0x02, 0x03, 0x01
};

/* 配置描述符：2 个接口（CDC 控制 + CDC 数据），wTotalLength = 0x43 */
static const uint8_t MyCfgDescr[] = {
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,

    /* 接口 0：CDC 控制 */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,             /* Header            */
    0x04, 0x24, 0x02, 0x02,                   /* ACM               */
    0x05, 0x24, 0x06, 0x00, 0x01,             /* Union(0,1)        */
    0x05, 0x24, 0x01, 0x00, 0x01,             /* Call Management   */
    0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x10, /* EP2 IN 中断 8B    */

    /* 接口 1：CDC 数据 */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00, /* EP3 IN  批量 64B  */
    0x07, 0x05, 0x03, 0x02, 0x40, 0x00, 0x00, /* EP3 OUT 批量 64B  */
};

static const uint8_t StrLang[] = {0x04, 0x03, 0x09, 0x04};
static const uint8_t StrVendor[] = {0x08, 0x03, 'W', 0, 'C', 0, 'H', 0};
static const uint8_t StrProduct[] = {
    0x1E, 0x03,
    'C', 0, 'H', 0, '5', 0, '9', 0, '2', 0, ' ', 0,
    'P', 0, 'A', 0, 'D', 0, ' ', 0, 'D', 0, 'I', 0, 'A', 0, 'G', 0
};
static const uint8_t StrSerial[] = {
    0x18, 0x03,
    'P', 0, 'A', 0, 'D', 0, 'D', 0, 'I', 0, 'A', 0, 'G', 0, '0', 0, '0', 0, '0', 0, '1', 0
};

/* 配置描述符总长必须等于 wTotalLength(0x43)，否则主机读到的是相邻常量 */
typedef char cdc_cfg_len_check[(sizeof(MyCfgDescr) == 0x43) ? 1 : -1];

/* ------------------------------------------------------------------ 状态 */

static uint8_t        DevConfig;
static uint8_t        SetupReqCode;
static uint16_t       SetupReqLen;
static const uint8_t *pDescr;
static uint8_t        Suspended;
static volatile uint8_t s_txBusy;

/* 主机读到的 Line Coding（115200 8N1）。
 * 注意：USB CDC 的波特率是**虚拟声明**，只影响主机端显示/打开时填的值，
 * 不决定任何实际速率；本固件真正的串口是 UART1，速率见 board.h 的
 * BOARD_UART_BAUDRATE（19200）。 */
static uint8_t s_lineCoding[7] = {0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08};

#define CDC_BUF_MASK    0x01FFu
static volatile uint8_t  s_buf[CDC_BUF_MASK + 1];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;

__attribute__((aligned(4))) static uint8_t EP0_Databuf[64];
__attribute__((aligned(4))) static uint8_t EP2_Databuf[128];
__attribute__((aligned(4))) static uint8_t EP3_Databuf[128];

/* ------------------------------------------------------------------ 发送缓冲 */

static uint16_t cdc_used(void)
{
    return (uint16_t)(s_head - s_tail);
}

void cdc_write(const uint8_t *data, uint16_t len)
{
    while(len--)
    {
        if(cdc_used() >= CDC_BUF_MASK + 1)
        {
            break; /* 缓冲满则丢弃新数据，不阻塞 */
        }
        s_buf[s_head & CDC_BUF_MASK] = *data++;
        s_head++;
    }
}

void cdc_puts(const char *s)
{
    const char *p = s;

    while(*p)
    {
        p++;
    }
    cdc_write((const uint8_t *)s, (uint16_t)(p - s));
}

void cdc_puthex8(uint8_t v)
{
    static const char h[] = "0123456789ABCDEF";
    uint8_t           b[2];

    b[0] = (uint8_t)h[v >> 4];
    b[1] = (uint8_t)h[v & 0x0F];
    cdc_write(b, 2);
}

void cdc_puthex16(uint16_t v)
{
    cdc_puthex8((uint8_t)(v >> 8));
    cdc_puthex8((uint8_t)(v & 0xFF));
}

void cdc_putu(uint32_t v)
{
    char    tmp[11];
    uint8_t n = 0;

    if(v == 0)
    {
        cdc_write((const uint8_t *)"0", 1);
        return;
    }
    while(v && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while(n)
    {
        cdc_write((const uint8_t *)&tmp[--n], 1);
    }
}

/* ------------------------------------------------------------------ 初始化 */

void cdc_init(void)
{
    pEP0_RAM_Addr = EP0_Databuf;
    pEP1_RAM_Addr = EP0_Databuf; /* EP1 未使用，避免空指针 */
    pEP2_RAM_Addr = EP2_Databuf;
    pEP3_RAM_Addr = EP3_Databuf;

    USB_DeviceInit();

    /* 只用 EP2 IN（通知）和 EP3 IN/OUT（数据）。
     * EP2/EP3 保持驱动默认的双向配置：硬件缓冲布局固定为
     * OUT 在基址、IN 在基址+64，改动使能位会影响 IN 数据位置。 */
    R8_UEP4_1_MOD = 0x00;
    R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN | RB_UEP3_RX_EN | RB_UEP3_TX_EN;

    R8_UEP1_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK;
    R8_UEP2_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP4_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK;

    PFIC_EnableIRQ(USB_IRQn);
}

void cdc_task(void)
{
    uint16_t used, i;
    uint8_t  n;

    if(!DevConfig || Suspended || s_txBusy)
    {
        return;
    }

    used = cdc_used();
    if(used == 0)
    {
        return;
    }

    n = (used > 64) ? 64 : (uint8_t)used;
    for(i = 0; i < n; i++)
    {
        pEP3_IN_DataBuf[i] = s_buf[(uint16_t)(s_tail + i) & CDC_BUF_MASK];
    }
    s_tail += n;

    s_txBusy = 1;
    DevEP3_IN_Deal(n);
}

/* ------------------------------------------------------------------ 中断处理 */

void USB_DevTransProcess(void)
{
    uint8_t  len, chtype;
    uint8_t  intflag, errflag = 0;
    uint8_t *pStr = NULL;

    intflag = R8_USB_INT_FG;

    if(intflag & RB_UIF_TRANSFER)
    {
        if((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN)
        {
            switch(R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP))
            {
                case UIS_TOKEN_IN: /* EP0 IN */
                    switch(SetupReqCode)
                    {
                        case USB_GET_DESCRIPTOR:
                            len = (SetupReqLen >= DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                            memcpy(pEP0_DataBuf, pDescr, len);
                            SetupReqLen -= len;
                            pDescr += len;
                            R8_UEP0_T_LEN = len;
                            R8_UEP0_CTRL ^= RB_UEP_T_TOG;
                            break;

                        case USB_SET_ADDRESS:
                            R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | SetupReqLen;
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                            break;

                        default:
                            R8_UEP0_T_LEN = 0;
                            R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                            break;
                    }
                    break;

                case UIS_TOKEN_OUT:
                    break;

                case UIS_TOKEN_IN | CDC_EP_DATA: /* 数据上传完成 */
                    R8_UEP3_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    s_txBusy = 0;
                    break;

                case UIS_TOKEN_OUT | CDC_EP_DATA: /* 主机下发数据，丢弃 */
                    if(R8_USB_INT_ST & RB_UIS_TOG_OK)
                    {
                        R8_UEP3_CTRL ^= RB_UEP_R_TOG;
                        len = R8_USB_RX_LEN;
                        (void)len;
                    }
                    break;

                case UIS_TOKEN_IN | CDC_EP_NOTIFY: /* 通知端点占位 */
                    R8_UEP2_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                    break;

                default:
                    break;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }

        if(R8_USB_INT_ST & RB_UIS_SETUP_ACT)
        {
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
            SetupReqLen = pSetupReqPak->wLength;
            SetupReqCode = pSetupReqPak->bRequest;
            chtype = pSetupReqPak->bRequestType;

            len = 0;
            errflag = 0;

            if((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS)
            {
                /* CDC 类请求 */
                switch(SetupReqCode)
                {
                    case 0x20: /* SET_LINE_CODING：数据阶段 7 字节，直接 ACK */
                        break;

                    case 0x21: /* GET_LINE_CODING */
                        memcpy(pEP0_DataBuf, s_lineCoding, sizeof(s_lineCoding));
                        break;

                    case 0x22: /* SET_CONTROL_LINE_STATE */
                        break;

                    case 0x23: /* SEND_BREAK */
                        break;

                    default:
                        errflag = 0xFF;
                        break;
                }
            }
            else if((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD)
            {
                switch(SetupReqCode)
                {
                    case USB_GET_DESCRIPTOR:
                        switch((pSetupReqPak->wValue) >> 8)
                        {
                            case USB_DESCR_TYP_DEVICE:
                                pDescr = MyDevDescr;
                                len = MyDevDescr[0];
                                break;

                            case USB_DESCR_TYP_CONFIG:
                                pDescr = MyCfgDescr;
                                len = MyCfgDescr[2];
                                break;

                            case USB_DESCR_TYP_STRING:
                                switch((pSetupReqPak->wValue) & 0xFF)
                                {
                                    case 0:
                                        pStr = (uint8_t *)StrLang;
                                        break;
                                    case 1:
                                        pStr = (uint8_t *)StrVendor;
                                        break;
                                    case 2:
                                        pStr = (uint8_t *)StrProduct;
                                        break;
                                    case 3:
                                        pStr = (uint8_t *)StrSerial;
                                        break;
                                    default:
                                        break;
                                }
                                if(pStr)
                                {
                                    pDescr = pStr;
                                    len = pStr[0];
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            default:
                                errflag = 0xFF;
                                break;
                        }

                        if(errflag == 0)
                        {
                            if(SetupReqLen > len)
                            {
                                SetupReqLen = len;
                            }
                            len = (SetupReqLen >= DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                            memcpy(pEP0_DataBuf, pDescr, len);
                            pDescr += len;
                        }
                        break;

                    case USB_SET_ADDRESS:
                        SetupReqLen = (pSetupReqPak->wValue) & 0xFF;
                        break;

                    case USB_GET_CONFIGURATION:
                        pEP0_DataBuf[0] = DevConfig;
                        if(SetupReqLen > 1)
                        {
                            SetupReqLen = 1;
                        }
                        break;

                    case USB_SET_CONFIGURATION:
                        DevConfig = (pSetupReqPak->wValue) & 0xFF;
                        break;

                    case USB_CLEAR_FEATURE:
                    case USB_SET_FEATURE:
                        break;

                    case USB_GET_INTERFACE:
                        pEP0_DataBuf[0] = 0x00;
                        if(SetupReqLen > 1)
                        {
                            SetupReqLen = 1;
                        }
                        break;

                    case USB_SET_INTERFACE:
                        break;

                    case USB_GET_STATUS:
                        pEP0_DataBuf[0] = 0x00;
                        pEP0_DataBuf[1] = 0x00;
                        if(SetupReqLen >= 2)
                        {
                            SetupReqLen = 2;
                        }
                        break;

                    default:
                        errflag = 0xFF;
                        break;
                }
            }
            else
            {
                errflag = 0xFF;
            }

            if(errflag == 0xFF)
            {
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
            }
            else
            {
                if(chtype & 0x80)
                {
                    len = (SetupReqLen > DevEP0SIZE) ? DevEP0SIZE : SetupReqLen;
                    SetupReqLen -= len;
                }
                else
                {
                    len = 0;
                }
                R8_UEP0_T_LEN = len;
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
            }

            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }
    }
    else if(intflag & RB_UIF_BUS_RST)
    {
        R8_USB_DEV_AD = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP2_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        DevConfig = 0;
        s_txBusy = 0;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }
    else if(intflag & RB_UIF_SUSPEND)
    {
        Suspended = (R8_USB_MIS_ST & RB_UMS_SUSPEND) ? 1 : 0;
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }
    else
    {
        R8_USB_INT_FG = intflag;
    }
}
__attribute__((interrupt("WCH-Interrupt-fast")))
__attribute__((section(".highcode")))
void USB_IRQHandler(void)
{
    USB_DevTransProcess();
}

#endif /* PAD_DIAG_CDC */
