/**
 * @file  keys.c
 * @brief 12 键扫描与消抖
 *
 * 极性按 board.h 里每个键的 active_high 字段区分：
 *   active_high = 0：内部上拉，按下接地（低电平）
 *   active_high = 1：内部下拉，按下接高（高电平，本板 K10/PB15）
 */

#include <string.h>

#include "keys.h"
#include "board.h"

/* 连续 3 次采样（约 15ms）一致才认为状态改变 */
#define KEY_DEBOUNCE_SAMPLES    3

static const board_key_t s_keys[KEY_COUNT] = BOARD_KEY_LIST;

static uint16_t s_stable;
static uint8_t  s_filter[KEY_COUNT];

void keys_init(void)
{
    uint8_t i;

    for(i = 0; i < KEY_COUNT; i++)
    {
        GPIOModeTypeDef mode = s_keys[i].active_high ? GPIO_ModeIN_PD : GPIO_ModeIN_PU;

        if(s_keys[i].port == KEY_PORT_A)
        {
            GPIOA_ModeCfg(s_keys[i].pin, mode);
        }
        else
        {
            GPIOB_ModeCfg(s_keys[i].pin, mode);
        }
    }

    s_stable = 0;
    memset(s_filter, 0, sizeof(s_filter));
}

uint16_t keys_scan_raw(void)
{
    uint32_t pa = R32_PA_PIN;
    uint32_t pb = R32_PB_PIN;
    uint16_t mask = 0;
    uint8_t  i;

    for(i = 0; i < KEY_COUNT; i++)
    {
        uint32_t level = (s_keys[i].port == KEY_PORT_A) ? (pa & s_keys[i].pin)
                                                        : (pb & s_keys[i].pin);

        if(s_keys[i].active_high)
        {
            if(level != 0)
            {
                mask |= s_keys[i].mask; /* 高电平 = 按下（本板 K10/PB15） */
            }
        }
        else if(level == 0)
        {
            mask |= s_keys[i].mask; /* 低电平 = 按下 */
        }
    }

    return mask;
}

uint16_t keys_poll(uint8_t *changed)
{
    uint16_t raw = keys_scan_raw();
    uint16_t stable = s_stable;
    uint8_t  i;

    for(i = 0; i < KEY_COUNT; i++)
    {
        uint16_t mask = s_keys[i].mask;
        uint8_t  raw_pressed = (raw & mask) ? 1 : 0;
        uint8_t  now_pressed = (stable & mask) ? 1 : 0;

        if(raw_pressed != now_pressed)
        {
            if(++s_filter[i] >= KEY_DEBOUNCE_SAMPLES)
            {
                s_filter[i] = 0;
                stable ^= mask;
            }
        }
        else
        {
            s_filter[i] = 0;
        }
    }

    if(changed)
    {
        *changed = (stable != s_stable) ? 1 : 0;
    }

    s_stable = stable;
    return stable;
}
