/**
 * @file  pad_ble.c
 * @brief BLE HID 游戏手柄应用层：广播数据、配对参数、HID 服务注册
 */

#include "CONFIG.h"
#include "hiddev.h"
#include "hidpadservice.h"
#include "bas_service.h"
#include "pad_ble.h"

/* --------------------------------------------------------------- 常量 */

#define PAD_DEVICE_NAME     "ESP-Mosaico MagPad"

/* 广播间隔 / 连接参数（单位 1.25ms） */
#define PAD_CONN_INTERVAL_MIN   6
#define PAD_CONN_INTERVAL_MAX   9
#define PAD_CONN_LATENCY        0
#define PAD_CONN_TIMEOUT        500     /* 单位 10ms */

/* 配对参数 */
#define PAD_PASSCODE            0
#define PAD_PAIRING_MODE        GAPBOND_PAIRING_MODE_WAIT_FOR_REQ
#define PAD_MITM_MODE           FALSE
#define PAD_BONDING_MODE        TRUE
#define PAD_IO_CAPABILITIES     GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT

/* --------------------------------------------------------------- 变量 */

static uint8_t advertData[] = {
    0x02, GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    0x03, GAP_ADTYPE_APPEARANCE,
    LO_UINT16(GAP_APPEARE_HID_GAMEPAD), HI_UINT16(GAP_APPEARE_HID_GAMEPAD)
};

static uint8_t scanRspData[] = {
    0x13, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'E', 'S', 'P', '-', 'M', 'o', 's', 'a', 'i', 'c', 'o', ' ', 'M', 'a', 'g', 'P', 'a', 'd',

    0x05, GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(PAD_CONN_INTERVAL_MIN), HI_UINT16(PAD_CONN_INTERVAL_MIN),
    LO_UINT16(PAD_CONN_INTERVAL_MAX), HI_UINT16(PAD_CONN_INTERVAL_MAX),

    0x03, GAP_ADTYPE_16BIT_MORE,
    LO_UINT16(HID_SERV_UUID), HI_UINT16(HID_SERV_UUID)
};

static const uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = PAD_DEVICE_NAME;

static hidDevCfg_t padHidCfg = {
    0,                 /* idle timeout: 关闭，游戏手柄不主动发空闲报表 */
    HID_FEATURE_FLAGS
};

/* HID 设备回调（全部交给默认实现即可） */
static uint8_t PadBleRptCB(uint8_t id, uint8_t type, uint16_t uuid,
                           uint8_t oper, uint16_t *pLen, uint8_t *pData);
static void    PadBleEvtCB(uint8_t evt);
static void    PadBleStateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);

static hidDevCB_t padHidCBs = {
    PadBleRptCB,
    PadBleEvtCB,
    NULL,
    PadBleStateCB
};

/* --------------------------------------------------------------- 实现 */

void PadBle_Init(void)
{
    /* 广播数据 */
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);

    /* GAP 设备名 */
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, (void *)attDeviceName);

    /* 连接参数（从机偏好） */
    {
        gapPeriConnectParams_t connParams;
        connParams.intervalMin = PAD_CONN_INTERVAL_MIN;
        connParams.intervalMax = PAD_CONN_INTERVAL_MAX;
        connParams.latency = PAD_CONN_LATENCY;
        connParams.timeout = PAD_CONN_TIMEOUT;
        GGS_SetParameter(GGS_PERI_CONN_PARAM_ATT, sizeof(gapPeriConnectParams_t), &connParams);
    }

    /* 绑定管理 */
    {
        uint32_t passkey = PAD_PASSCODE;
        uint8_t  pairMode = PAD_PAIRING_MODE;
        uint8_t  mitm = PAD_MITM_MODE;
        uint8_t  ioCap = PAD_IO_CAPABILITIES;
        uint8_t  bonding = PAD_BONDING_MODE;

        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    /* HID 服务与 HID 设备 */
    HidPad_AddService();

    /* 标准电池服务（0x180F），电量由 main.c 的电池检测更新 */
    Bas_AddService();

    HidDev_Register(&padHidCfg, &padHidCBs);
}

void PadBle_SendReport(const uint8_t payload[PAD_REPORT_LEN])
{
    HidDev_Report(HID_RPT_ID_PAD_IN, HID_REPORT_TYPE_INPUT, PAD_REPORT_LEN, (uint8_t *)payload);
}

/* --------------------------------------------------------------- 回调 */

static uint8_t PadBleRptCB(uint8_t id, uint8_t type, uint16_t uuid,
                           uint8_t oper, uint16_t *pLen, uint8_t *pData)
{
    uint8_t status = SUCCESS;

    if(oper == HID_DEV_OPER_READ)
    {
        status = HidPad_GetParameter(id, type, uuid, pLen, pData);
    }
    else if(oper == HID_DEV_OPER_WRITE)
    {
        status = HidPad_SetParameter(id, type, uuid, *pLen, pData);
    }

    return status;
}

static void PadBleEvtCB(uint8_t evt)
{
    /* 游戏手柄没有 Boot 报表：若主机写入 Boot Protocol，强制留在 Report
     * Protocol，否则 hiddev 会按 boot 模式查找报表而找不到，导致不再上报。 */
    if(evt == HID_DEV_SET_BOOT_EVT)
    {
        hidProtocolMode = HID_PROTOCOL_MODE_REPORT;
    }
}

/* GAP 状态回调：广播超时/连接断开后重新打开广播，保证随时可被搜索到 */
static void PadBleStateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    (void)pEvent;

    switch(newState & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_WAITING:
        {
            uint8_t enable = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &enable);
            break;
        }

        default:
            break;
    }
}
