/**
 * @file  hidpadservice.c
 * @brief BLE HID 游戏手柄服务
 *
 * 结构参考 WCH HID_Keyboard 例程的 hidkbdservice.c，去掉键盘 Boot 报表、
 * 电池服务与 Feature 报表，只保留游戏手柄需要的：
 *   HID Information / Control Point / Protocol Mode / Report Map /
 *   Input Report(Notify) + Report Reference + CCCD
 */

#include "CONFIG.h"
#include "hiddev.h"
#include "hidpadservice.h"
#include "pad_hid.h"

/* ------------------------------------------------------------------ UUID */

static const uint8_t hidServUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(HID_SERV_UUID), HI_UINT16(HID_SERV_UUID)};

static const uint8_t hidInfoUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(HID_INFORMATION_UUID), HI_UINT16(HID_INFORMATION_UUID)};

static const uint8_t hidReportMapUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(REPORT_MAP_UUID), HI_UINT16(REPORT_MAP_UUID)};

static const uint8_t hidControlPointUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(HID_CTRL_PT_UUID), HI_UINT16(HID_CTRL_PT_UUID)};

static const uint8_t hidProtocolModeUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(PROTOCOL_MODE_UUID), HI_UINT16(PROTOCOL_MODE_UUID)};

static const uint8_t hidReportUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(REPORT_UUID), HI_UINT16(REPORT_UUID)};

/* ------------------------------------------------------------------ 属性值 */

/* HID Information：bcdHID = 1.11, bCountryCode = 0, Flags */
static const uint8_t hidInfo[HID_INFORMATION_LEN] = {
    LO_UINT16(0x0111), HI_UINT16(0x0111), 0x00, HID_FEATURE_FLAGS};

/* Report Map：直接复用 pad_hid 中的 BLE 报表描述符（带 Report ID） */
uint16_t hidReportMapLen = 0;

/* Report mapping table */
static hidRptMap_t hidRptMap[HID_NUM_REPORTS];

/* HID Service */
static const gattAttrType_t hidService = {ATT_BT_UUID_SIZE, hidServUUID};

static uint8_t hidInfoProps = GATT_PROP_READ;

static uint8_t hidControlPointProps = GATT_PROP_WRITE_NO_RSP;
static uint8_t hidControlPoint;

static uint8_t hidProtocolModeProps = GATT_PROP_READ | GATT_PROP_WRITE_NO_RSP;
uint8_t        hidProtocolMode = HID_PROTOCOL_MODE_REPORT;

static uint8_t       hidReportMapProps = GATT_PROP_READ;

static uint8_t       hidReportInProps = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t       hidReportIn;
static gattCharCfg_t hidReportInClientCharCfg[GATT_MAX_NUM_CONN];

static uint8_t hidReportRefIn[HID_REPORT_REF_LEN] = {HID_RPT_ID_PAD_IN, HID_REPORT_TYPE_INPUT};

/* ------------------------------------------------------------------ 属性表 */

static gattAttribute_t hidAttrTbl[] = {
    /* HID Service */
    {
        {ATT_BT_UUID_SIZE, primaryServiceUUID},
        GATT_PERMIT_READ,
        0,
        (uint8_t *)&hidService
    },

    /* HID Information */
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidInfoProps
    },
    {
        {ATT_BT_UUID_SIZE, hidInfoUUID},
        GATT_PERMIT_ENCRYPT_READ,
        0,
        (uint8_t *)hidInfo
    },

    /* HID Control Point */
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidControlPointProps
    },
    {
        {ATT_BT_UUID_SIZE, hidControlPointUUID},
        GATT_PERMIT_ENCRYPT_WRITE,
        0,
        &hidControlPoint
    },

    /* HID Protocol Mode */
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidProtocolModeProps
    },
    {
        {ATT_BT_UUID_SIZE, hidProtocolModeUUID},
        GATT_PERMIT_ENCRYPT_READ | GATT_PERMIT_ENCRYPT_WRITE,
        0,
        &hidProtocolMode
    },

    /* HID Report Map */
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidReportMapProps
    },
    {
        {ATT_BT_UUID_SIZE, hidReportMapUUID},
        GATT_PERMIT_ENCRYPT_READ,
        0,
        (uint8_t *)pad_hid_report_map_ble
    },

    /* HID Input Report */
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &hidReportInProps
    },
    {
        {ATT_BT_UUID_SIZE, hidReportUUID},
        GATT_PERMIT_ENCRYPT_READ,
        0,
        &hidReportIn
    },
    {
        {ATT_BT_UUID_SIZE, clientCharCfgUUID},
        GATT_PERMIT_READ | GATT_PERMIT_ENCRYPT_WRITE,
        0,
        (uint8_t *)&hidReportInClientCharCfg
    },
    {
        {ATT_BT_UUID_SIZE, reportRefUUID},
        GATT_PERMIT_READ,
        0,
        hidReportRefIn
    },
};

/* 属性索引 */
enum
{
    HID_SERVICE_IDX,
    HID_INFO_DECL_IDX,
    HID_INFO_IDX,
    HID_CONTROL_POINT_DECL_IDX,
    HID_CONTROL_POINT_IDX,
    HID_PROTOCOL_MODE_DECL_IDX,
    HID_PROTOCOL_MODE_IDX,
    HID_REPORT_MAP_DECL_IDX,
    HID_REPORT_MAP_IDX,
    HID_REPORT_IN_DECL_IDX,
    HID_REPORT_IN_IDX,
    HID_REPORT_IN_CCCD_IDX,
    HID_REPORT_REF_IN_IDX
};

/* 服务回调 */
static gattServiceCBs_t hidPadCBs = {
    HidDev_ReadAttrCB,
    HidDev_WriteAttrCB,
    NULL
};

/* ------------------------------------------------------------------ 实现 */

bStatus_t HidPad_AddService(void)
{
    bStatus_t status;

    hidReportMapLen = pad_hid_report_map_ble_len;

    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, hidReportInClientCharCfg);

    status = GATTServApp_RegisterService(hidAttrTbl, GATT_NUM_ATTRS(hidAttrTbl),
                                         GATT_MAX_ENCRYPT_KEY_SIZE, &hidPadCBs);
    if(status != SUCCESS)
    {
        return status;
    }

    hidRptMap[0].id = hidReportRefIn[0];
    hidRptMap[0].type = hidReportRefIn[1];
    hidRptMap[0].handle = hidAttrTbl[HID_REPORT_IN_IDX].handle;
    hidRptMap[0].cccdHandle = hidAttrTbl[HID_REPORT_IN_CCCD_IDX].handle;
    hidRptMap[0].mode = HID_PROTOCOL_MODE_REPORT;

    HidDev_RegisterReports(HID_NUM_REPORTS, hidRptMap);

    return SUCCESS;
}

uint8_t HidPad_SetParameter(uint8_t id, uint8_t type, uint16_t uuid, uint8_t len, void *pValue)
{
    (void)id;
    (void)type;
    (void)uuid;
    (void)len;
    (void)pValue;

    /* 本服务没有输出/特征报表 */
    return ATT_ERR_ATTR_NOT_FOUND;
}

uint8_t HidPad_GetParameter(uint8_t id, uint8_t type, uint16_t uuid, uint16_t *pLen, void *pValue)
{
    (void)id;
    (void)type;
    (void)uuid;
    (void)pValue;

    *pLen = 0;
    return SUCCESS;
}
