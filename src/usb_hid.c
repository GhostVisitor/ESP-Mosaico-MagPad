/**
 * @file  usb_hid.c
 * @brief USB HID 游戏手柄（CH592 内置 USB 设备控制器，Full-Speed）
 *
 * 描述符：1 个 HID 接口，EP1 IN（中断，8 字节），Report ID = 1。
 * 枚举完成（SET_CONFIGURATION）后 usb_hid_ready() 返回 1。
 */

#include "usb_hid.h"
#include "pad_hid.h"
#include "CH59x_usbdev.h"

/* 诊断固件（PAD_DIAG_CDC）里 USB 由 usb_cdc.c 接管，这里整体不参与编译，
 * 避免两个 USB_IRQHandler 重复定义 */
#ifndef PAD_DIAG_CDC

#define DevEP0SIZE      0x40
#define DevEP1SIZE      8

/* ------------------------------------------------------------------ 描述符 */

/* 设备描述符：与 ESP-Mosaico 参考固件一致，VID 0x0079 / PID 0x0006
 * （通用 HID 手柄 ID，Windows 免驱；如要用自己的 ID 改这里即可） */
const uint8_t MyDevDescr[] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, DevEP0SIZE,
    0x79, 0x00, 0x06, 0x00, 0x00, 0x01,
    0x01, 0x02, 0x03, 0x01
};

/* 配置描述符：9 + 9(接口) + 9(HID) + 7(端点) = 34
 * HID 描述符里的 wDescriptorLength（偏移 25/26）在 usb_hid_init() 里
 * 按 pad_hid_report_map_usb_len 的真实长度填写。 */
static uint8_t MyCfgDescr[] = {
    0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x00, 0x00,
    0x07, 0x05, 0x81, 0x03, DevEP1SIZE, 0x00, 0x01
};

#define CFG_HID_LEN_LO_IDX      25
#define CFG_HID_LEN_HI_IDX      26

/* 字符串描述符 */
static const uint8_t StrLang[] = {0x04, 0x03, 0x09, 0x04};
static const uint8_t StrVendor[] = {0x18, 0x03, 'E', 0, 'S', 0, 'P', 0, '-', 0,
                                    'M', 0, 'o', 0, 's', 0, 'a', 0, 'i', 0, 'c', 0, 'o', 0};
static const uint8_t StrProduct[] = {
    0x26, 0x03,
    'E', 0, 'S', 0, 'P', 0, '-', 0, 'M', 0, 'o', 0, 's', 0,
    'a', 0, 'i', 0, 'c', 0, 'o', 0, ' ', 0, 'M', 0, 'a', 0,
    'g', 0, 'P', 0, 'a', 0, 'd', 0
};
static const uint8_t StrSerial[] = {0x10, 0x03, 'P', 0, 'A', 0, 'D', 0, '0', 0, '0', 0, '0', 0, '1', 0};

/* 报表描述符长度不写死：usb_hid_init() 会把 pad_hid_report_map_usb_len
 * 填进 MyCfgDescr[CFG_HID_LEN_LO_IDX/HI_IDX]，改描述符时无需手改这里。 */

/* ------------------------------------------------------------------ 状态 */

static uint8_t DevConfig;
static uint8_t SetupReqCode;
static uint16_t SetupReqLen;
static const uint8_t *pDescr;
static uint8_t IdleValue;
static uint8_t ProtocolValue = 1; /* 1 = Report Protocol */
static uint8_t Suspended;

__attribute__((aligned(4))) static uint8_t EP0_Databuf[64];
__attribute__((aligned(4))) static uint8_t EP1_Databuf[64 + 64];

/* ------------------------------------------------------------------ 内部 */

void usb_hid_init(void)
{
    /* 填入报表描述符真实长度 */
    MyCfgDescr[CFG_HID_LEN_LO_IDX] = (uint8_t)(pad_hid_report_map_usb_len & 0xFF);
    MyCfgDescr[CFG_HID_LEN_HI_IDX] = (uint8_t)((pad_hid_report_map_usb_len >> 8) & 0xFF);

    pEP0_RAM_Addr = EP0_Databuf;
    pEP1_RAM_Addr = EP1_Databuf;

    USB_DeviceInit();

    /* 只使用 EP1 IN：关掉其它端点通道，避免未初始化的 DMA 地址被意外访问 */
    R8_UEP2_3_MOD = 0x00;
    R8_UEP4_1_MOD = RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8_UEP2_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK;
    R8_UEP3_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK;
    R8_UEP4_CTRL = UEP_R_RES_NAK | UEP_T_RES_NAK;

    PFIC_EnableIRQ(USB_IRQn);
}

uint8_t usb_hid_ready(void)
{
    return (DevConfig != 0 && Suspended == 0) ? 1 : 0;
}

void usb_hid_send_report(const uint8_t payload[PAD_REPORT_LEN])
{
    if(!usb_hid_ready())
    {
        return;
    }

    /* USB 报表不带 Report ID，直接发 7 字节 */
    memcpy(pEP1_IN_DataBuf, payload, PAD_REPORT_LEN);
    DevEP1_IN_Deal(PAD_REPORT_LEN);
}

/* ------------------------------------------------------------------ 中断处理 */

void USB_DevTransProcess(void)
{
    uint8_t len, chtype;
    uint8_t intflag, errflag = 0;
    uint8_t *pStr = NULL;

    intflag = R8_USB_INT_FG;

    if(intflag & RB_UIF_TRANSFER)
    {
        if((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN)
        {
            switch(R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP))
            {
                case UIS_TOKEN_IN: /* 端点 0 IN：控制传输的数据/状态阶段 */
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

                case UIS_TOKEN_IN | 1: /* 端点 1 IN：上报完成，等待下一次数据 */
                    R8_UEP1_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
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

            if((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
            {
                /* HID 类请求 */
                if((pSetupReqPak->bRequestType & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS)
                {
                    switch(SetupReqCode)
                    {
                        case DEF_USB_GET_IDLE:
                            EP0_Databuf[0] = IdleValue;
                            len = 1;
                            break;

                        case DEF_USB_SET_IDLE:
                            IdleValue = (uint8_t)(pSetupReqPak->wValue & 0xFF);
                            break;

                        case DEF_USB_GET_PROTOCOL:
                            EP0_Databuf[0] = ProtocolValue;
                            len = 1;
                            break;

                        case DEF_USB_SET_PROTOCOL:
                            ProtocolValue = (uint8_t)(pSetupReqPak->wValue & 0xFF);
                            break;

                        case DEF_USB_SET_REPORT:
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
            }
            else
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

                            case USB_DESCR_TYP_HID:
                                pDescr = (uint8_t *)(&MyCfgDescr[18]);
                                len = 9;
                                break;

                            case USB_DESCR_TYP_REPORT:
                                pDescr = pad_hid_report_map_usb;
                                len = (uint8_t)pad_hid_report_map_usb_len;
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
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                        {
                            if(((pSetupReqPak->wIndex) & 0xFF) == 0x81)
                            {
                                R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                        }
                        break;

                    case USB_SET_FEATURE:
                        if((pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                        {
                            if(((pSetupReqPak->wIndex) & 0xFF) == 0x81)
                            {
                                R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_STALL;
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                        }
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
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        DevConfig = 0;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }
    else if(intflag & RB_UIF_SUSPEND)
    {
        if(R8_USB_MIS_ST & RB_UMS_SUSPEND)
        {
            Suspended = 1;
        }
        else
        {
            Suspended = 0;
        }
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