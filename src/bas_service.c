/**
 * @file  bas_service.c
 * @brief 标准电池服务（Battery Service 0x180F / Battery Level 0x2A19）
 *
 * 结构参考 WCH HID 例程的 battservice.c，去掉 HID 报表映射和 ADC 测量，
 * 只保留标准 BAS：电量由应用层（battery.c 的电压判断）通过 Bas_SetLevel()
 * 写入，变化时通知已订阅的主机。
 */

#include "CONFIG.h"
#include "bas_service.h"

#define BAS_LEVEL_LEN       1

/* 属性表索引 */
#define BAS_SERVICE_IDX     0
#define BAS_LEVEL_DECL_IDX  1
#define BAS_LEVEL_VALUE_IDX 2
#define BAS_LEVEL_CCCD_IDX  3

/* ---------------------------------------------------------------- UUID */

static const uint8_t basServUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(BATT_SERV_UUID), HI_UINT16(BATT_SERV_UUID)};

static const uint8_t basLevelUUID[ATT_BT_UUID_SIZE] = {
    LO_UINT16(BATT_LEVEL_UUID), HI_UINT16(BATT_LEVEL_UUID)};

/* ---------------------------------------------------------------- 属性 */

static const gattAttrType_t basService = {ATT_BT_UUID_SIZE, basServUUID};

static uint8_t       basLevelProps = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t       basLevel = 100;
static gattCharCfg_t basLevelCfg[GATT_MAX_NUM_CONN];

static gattAttribute_t basAttrTbl[] = {
    /* Battery Service */
    {
        {ATT_BT_UUID_SIZE, primaryServiceUUID},
        GATT_PERMIT_READ,
        0,
        (uint8_t *)&basService
    },

    /* Battery Level 特征声明 */
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &basLevelProps
    },

    /* Battery Level 值 */
    {
        {ATT_BT_UUID_SIZE, basLevelUUID},
        GATT_PERMIT_READ,
        0,
        &basLevel
    },

    /* Battery Level CCCD */
    {
        {ATT_BT_UUID_SIZE, clientCharCfgUUID},
        GATT_PERMIT_READ | GATT_PERMIT_WRITE,
        0,
        (uint8_t *)basLevelCfg
    },
};

/* ---------------------------------------------------------------- 回调 */

static bStatus_t basReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                               uint8_t *pValue, uint16_t *pLen, uint16_t offset,
                               uint16_t maxLen, uint8_t method);
static bStatus_t basWriteAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                uint8_t *pValue, uint16_t len, uint16_t offset,
                                uint8_t method);
static void      basNotifyCB(linkDBItem_t *pLinkItem);

static gattServiceCBs_t basCBs = {
    basReadAttrCB,
    basWriteAttrCB,
    NULL
};

/* ---------------------------------------------------------------- 实现 */

void Bas_AddService(void)
{
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, basLevelCfg);
    GATTServApp_RegisterService(basAttrTbl, GATT_NUM_ATTRS(basAttrTbl),
                                GATT_MAX_ENCRYPT_KEY_SIZE, &basCBs);
}

void Bas_SetLevel(uint8_t level)
{
    if(level > 100)
    {
        level = 100;
    }
    if(level == basLevel)
    {
        return;
    }

    basLevel = level;
    linkDB_PerformFunc(basNotifyCB);
}

uint8_t Bas_GetLevel(void)
{
    return basLevel;
}

/* ---------------------------------------------------------------- 内部 */

static bStatus_t basReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                               uint8_t *pValue, uint16_t *pLen, uint16_t offset,
                               uint16_t maxLen, uint8_t method)
{
    uint16_t uuid;

    (void)connHandle;
    (void)maxLen;
    (void)method;

    if(offset > 0)
    {
        return ATT_ERR_ATTR_NOT_LONG;
    }

    uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

    if(uuid == BATT_LEVEL_UUID)
    {
        *pLen = BAS_LEVEL_LEN;
        pValue[0] = basLevel;
        return SUCCESS;
    }

    return ATT_ERR_ATTR_NOT_FOUND;
}

static bStatus_t basWriteAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                uint8_t *pValue, uint16_t len, uint16_t offset,
                                uint8_t method)
{
    uint16_t uuid;

    (void)method;

    uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

    if(uuid == GATT_CLIENT_CHAR_CFG_UUID)
    {
        return GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
                                              offset, GATT_CLIENT_CFG_NOTIFY);
    }

    return ATT_ERR_ATTR_NOT_FOUND;
}

static void basNotifyCB(linkDBItem_t *pLinkItem)
{
    attHandleValueNoti_t noti;

    if(!(pLinkItem->stateFlags & LINK_CONNECTED))
    {
        return;
    }

    if(!(GATTServApp_ReadCharCfg(pLinkItem->connectionHandle, basLevelCfg) & GATT_CLIENT_CFG_NOTIFY))
    {
        return;
    }

    noti.pValue = GATT_bm_alloc(pLinkItem->connectionHandle, ATT_HANDLE_VALUE_NOTI,
                                BAS_LEVEL_LEN, NULL, 0);
    if(noti.pValue == NULL)
    {
        return;
    }

    noti.handle = basAttrTbl[BAS_LEVEL_VALUE_IDX].handle;
    noti.len = BAS_LEVEL_LEN;
    noti.pValue[0] = basLevel;

    if(GATT_Notification(pLinkItem->connectionHandle, &noti, FALSE) != SUCCESS)
    {
        GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
    }
}
