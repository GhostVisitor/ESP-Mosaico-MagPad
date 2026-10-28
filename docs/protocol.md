# 串口协议与按键对照表

面向**上位机（主控单片机）**开发者。手柄通过 `PA9 (TXD1)` 单向发送数据，
`PA8 (RXD1)` 预留但当前固件不解析。

---

## 1. 物理层

| 项 | 值 |
|---|---|
| 接口 | UART1，单向发送（手柄只发） |
| 引脚 | 手柄 `PA9/TXD1` → 主控 `RX`（需共地） |
| 电平 | 3.3V TTL |
| 格式 | **19200 bps，8 数据位，无校验，1 停止位** |
| 流控 | 无 |

---

## 2. 按键帧（定长 5 字节）

```
字节0   字节1   字节2     字节3     字节4
0xA5    0x5A    LO        HI        XOR
帧头1   帧头2   掩码低8位  掩码高4位  校验
```

- `LO = mask & 0xFF`
- `HI = (mask >> 8) & 0x0F`（只用低 4 位，bit4~7 恒为 0）
- `XOR = 0xA5 ^ 0x5A ^ LO ^ HI`

> ⚠️ 与 ESP-Mosaico（CH552T 版）的区别：本版固件把 `HI` **裁成低 4 位**（`& 0x0F`）。
> 12 键时两种写法数值相同，但**校验和必须按裁剪后的 `HI` 计算**。
> 直接复用 CH552 版的主控解析代码时，请先确认这一点。

`mask` 是 12 位键位掩码，**bit n = KEY_(n+1) 按下**（1 = 按下，0 = 松开）。

### 发送时机

| 场景 | 行为 |
|---|---|
| 键位变化 | 立即发一帧 |
| 键位不变 | 每 **400ms** 发一帧心跳（内容不变） |
| 上电 | 先发一帧 `A5 5A 00 00 FF` |

> 心跳用于让上位机判断面板在线：超过 1~2s 收不到任何合法帧即可判定离线。

---

## 3. 按键掩码表

| bit | mask | 按键 | 手柄功能 | 典型帧（仅该键按下） |
|:---:|---:|---|---|---|
| 0 | `0x0001` | KEY_1 | D-Pad 上 (hat=0) | `A5 5A 01 00 FE` |
| 1 | `0x0002` | KEY_2 | D-Pad 左 (hat=6) | `A5 5A 02 00 FD` |
| 2 | `0x0004` | KEY_3 | D-Pad 下 (hat=4) | `A5 5A 04 00 FB` |
| 3 | `0x0008` | KEY_4 | D-Pad 右 (hat=2) | `A5 5A 08 00 F7` |
| 4 | `0x0010` | KEY_5 | Y | `A5 5A 10 00 EF` |
| 5 | `0x0020` | KEY_6 | X | `A5 5A 20 00 DF` |
| 6 | `0x0040` | KEY_7 | A | `A5 5A 40 00 BF` |
| 7 | `0x0080` | KEY_8 | B | `A5 5A 80 00 7F` |
| 8 | `0x0100` | KEY_9 | Back / SELECT | `A5 5A 00 01 FE` |
| 9 | `0x0200` | KEY_10 | Start | `A5 5A 00 02 FD` |
| 10 | `0x0400` | KEY_11 | LT | `A5 5A 00 04 FB` |
| 11 | `0x0800` | KEY_12 | RT | `A5 5A 00 08 F7` |
| — | `0x0000` | 全松开 | 空闲 | `A5 5A 00 00 FF` |

组合键按位或，例如 `UP + A`（`0x0041`）→ `A5 5A 41 00 BE`。

---

## 4. 低电告警帧（可选解析）

电池电压低于 3.0V（连续 3 次确认）后，除按键帧外每秒额外发送：

```
0xA5  0x5B  mv_L  mv_H  XOR
```

- `mv = 0..6000`，单位 mV；
- `XOR = 0xA5 ^ 0x5B ^ mv_L ^ mv_H`。

例：2980mV = `0x0BA4` → `A5 5B A4 0B 55`。

第二字节用 `0x5B` 而不是按键帧的 `0x5A`，**旧解析器会把它当乱序自动丢弃**，
不会误判成按键。

---

## 5. 上位机解析代码

```c
#include <stdint.h>

#define KP_HEAD0   0xA5
#define KP_HEAD1   0x5A
#define KP_ALARM   0x5B

volatile uint16_t g_key_mask = 0;   /* bit n = KEY_(n+1) 按下 */
volatile uint16_t g_bat_mv   = 0;   /* 最近一次低电告警电压，0 = 未知 */

void KP_feed(uint8_t b)
{
    static uint8_t st = 0, cmd = 0, lo = 0, hi = 0;

    switch (st) {
    case 0:
        if (b == KP_HEAD0) st = 1;
        break;
    case 1:
        if (b == KP_HEAD1)      { cmd = 0; st = 2; }
        else if (b == KP_ALARM) { cmd = 1; st = 2; }
        else st = 0;
        break;
    case 2:
        lo = b; st = 3;
        break;
    case 3:
        hi = b; st = 4;
        break;
    case 4:
        if (b == (uint8_t)(KP_HEAD0 ^ (cmd ? KP_ALARM : KP_HEAD1) ^ lo ^ hi)) {
            if (cmd == 0) g_key_mask = (uint16_t)lo | ((uint16_t)hi << 8);
            else          g_bat_mv   = (uint16_t)lo | ((uint16_t)hi << 8);
        }
        st = 0;
        break;
    default:
        st = 0;
        break;
    }
}

#define KEY_DOWN(n)   ((g_key_mask >> (n)) & 1u)   /* n = 0..11 */
```

要点：

1. **必须校验 XOR**，校验通过才更新状态；
2. 状态机逐字节推进，乱码会自动回到等帧头；
3. `g_key_mask` 是全量状态（不是事件），丢一两帧也能在下一帧自动恢复；
4. 需要“按下瞬间触发一次”的语义时，在上位机做边沿检测；
5. 手柄侧已有 ≈15ms 消抖，上位机不需要再消抖。
