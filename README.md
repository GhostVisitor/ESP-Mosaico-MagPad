# ESP-Mosaico 磁吸手柄

基于 **CH592F**（QFN28，RISC-V + BLE）的 12 键磁吸手柄控制板：

- **USB**：插上就是免驱 HID 手柄；
- **BLE**：同时作为 BLE HID 手柄广播，可连手机 / 电脑；
- **串口**：把按键掩码以 5 字节帧实时上报给上位机（19200 8N1）；
- **电池管理**：内部 ADC 测电池电压，低电时串口 + BLE 电量 + 红色 LED 三重提示，
  10 秒后自动下电。

串口协议、按键掩码和解析代码见 **[docs/protocol.md](docs/protocol.md)**。

---

## 1. 功能特性

| 功能 | 说明 |
|---|---|
| 12 个按键 | 内部上下拉，软件消抖 ≈15ms；K10(PB15) 为高电平按下，其余低电平 |
| USB HID | VID:PID `0x0079:0x0006`，7 字节报表，W3C 标准手柄布局（A B X Y LB RB LT RT Back Start） |
| BLE HID | HID over GATT（0x1812），Report ID 1，报表内容与 USB 相同 |
| 串口 | 19200 8N1，`A5 5A <mask>` 5 字节帧；状态变化立即发，空闲 400ms 心跳 |
| 电池检测 | CH592 内部 VBAT 通道（ADC 通道 14），每秒采样 |
| 低电告警 | < 3.0V 连续 3 次：串口 `A5 5B` 帧 + BLE 电量 5% + 10 秒后自动下电 |
| BLE 电池服务 | 标准 BAS（0x180F / 0x2A19），100% / 5% 两档 |
| 指示灯 | PB23 红色 LED，高电平点亮；运行常亮，< 3.3V 时 0.25Hz 闪烁 |
| 关机 | 长按 SELECT 5 秒；15 分钟无按键自动关机；PB15 边沿唤醒 |
| 清配对 | 同时按住 SELECT + START 3 秒 |

---

## 2. 硬件

### 2.1 供电

单节锂电池 → 3.3V LDO → CH592F `VDD33`。`PB14 = LDO_EN`，固件上电第一件事就是
把它拉高让 LDO 自保持。

另有一路 **外部 EN** 参与电源控制（硬件行为，固件不判断）：外部电源在线时，
固件拉低 PB14 后 MCU 仍有电，可以进入 Shutdown 并由 PB15 唤醒；外部电源不在线时，
拉低 PB14 直接切断整机供电。

### 2.2 引脚分配（CH592F QFN28）

| 功能 | 引脚 | GPIO | 说明 |
|---|---|---|---|
| LDO_EN | 9 | PB14 | 上电立即输出高，LDO 自保持 |
| 红色 LED | 16 | PB23 | 高电平点亮，运行常亮 / 低电闪烁 |
| USB D- | 13 | PB10 / UD- | USB 设备 |
| USB D+ | 12 | PB11 / UD+ | USB 设备 |
| UART TX | 7 | PA9 / TXD1 | 网络标号 U0TXD，19200 8N1 |
| UART RX | 6 | PA8 / RXD1 | 网络标号 U0RXD（当前固件不解析） |
| 32M 晶振 | 19 / 18 | X32MI / X32MO | BLE 射频时钟（必需） |
| 32.768k 晶振 | 2 / 1 | PA10 / PA11 | 可选，默认用内部 RC |
| K1 UP | 23 | PA4 | mask 0x0001 |
| K2 LEFT | 24 | PA5 | mask 0x0002 |
| K3 DOWN | 28 | PA12 | mask 0x0004 |
| K4 RIGHT | 27 | PA13 | mask 0x0008 |
| K5 Y | 26 | PA14 | mask 0x0010 |
| K6 X | 25 | PA15 | mask 0x0020 |
| K7 A | 15 | PB4 | mask 0x0040 |
| K8 B | 14 | PB7 | mask 0x0080 |
| K9 SELECT | 17 | PB22 | mask 0x0100 |
| K10 START | 8 | PB15 | mask 0x0200，**高电平按下** |
| K11 LT | 11 | PB12 | mask 0x0400 |
| K12 RT | 10 | PB13 | mask 0x0800 |

> 网络标号是 `U0RXD/U0TXD`，但 PA8/PA9 在 CH592F 上只有 UART1 功能，
> 所以固件使用 **UART1**。

### 2.3 调试口注意

CH592 复位后 `CFG_DEBUG_EN` 默认为 1，此时 **PB14/PB15 只能作为 TIO/TCK，
不能当 GPIO**。本板 PB14 是 LDO_EN、PB15 是 K10，所以：

- 固件启动时会写 `RB_DEBUG_EN` 关闭调试接口，把两个脚释放为 GPIO；
- 量产建议用 WCH-LinkUtility 把 `CFG_DEBUG_EN` 直接烧成 **0**，否则复位后到
  固件执行之间的极短时间内 PB14 仍处于调试口状态。

---

## 3. 目录结构

```
.
├── Makefile              # 命令行构建
├── src/                  # 应用源码
│   ├── main.c            # 初始化、5ms 扫描任务、关机/低电/指示灯逻辑
│   ├── board.c/.h        # 引脚定义、上电时序、LED
│   ├── keys.c/.h         # 12 键扫描 + 消抖（逐键极性）
│   ├── pad_hid.c/.h      # HID 报表描述符与按键映射
│   ├── usb_hid.c/.h      # USB HID 设备
│   ├── usb_cdc.c/.h      # USB CDC（仅诊断固件）
│   ├── diag.c/.h         # 诊断固件入口
│   ├── pad_ble.c/.h      # BLE 从机应用（广播、配对、HID 服务）
│   ├── hidpadservice.c/.h# HID over GATT 服务（0x1812）
│   ├── hiddev.c/.h       # WCH HID 设备管理（已裁剪电池/扫描参数）
│   ├── devinfoservice.c/.h
│   ├── bas_service.c/.h  # 标准电池服务（0x180F）
│   ├── battery.c/.h      # 内部 VBAT 通道测电压
│   └── uart_link.c/.h    # 串口帧（按键 + 低电告警）
├── sdk/ch59x/            # WCH CH592 EVT 代码/库子集
├── scripts/build.ps1     # 一键构建（自动找 MounRiver 工具链）
├── docs/protocol.md      # 串口协议 + 按键对照 + 解析代码
├── hardware/             # 原理图 / PCB / 外壳（自行补充）
└── prebuilt/             # 预编译固件
```

---

## 4. 编译

需要 **MounRiver Studio**（**2 或 3 均可**，内置 `riscv-none-embed-gcc 8.2.0` 和 `make`）。
`build.ps1` 会先找 `-MrsRoot` 指定的目录，没找到就自动探测
`C:\MounRiver\MounRiver_Studio2` / `...Studio3` 等常见安装位置。

```powershell
pwsh -File scripts/build.ps1            # 正常固件
pwsh -File scripts/build.ps1 -Diag      # USB CDC 诊断固件
pwsh -File scripts/build.ps1 -Clean     # 先清理
pwsh -File scripts/build.ps1 -MrsRoot "C:\MounRiver\MounRiver_Studio2"   # 手动指定安装目录
```

或直接：

```bash
make                                    # build/PAD_CH592.hex
make TARGET=build/PAD_DIAG OBJ_DIR=build/obj_diag EXTRA_CFLAGS=-DPAD_DIAG_CDC
```

产物：

| 文件 | 说明 |
|---|---|
| `build/PAD_CH592.hex` | 正常固件 |
| `build/PAD_DIAG.hex` | 诊断固件（USB CDC 虚拟串口，打印状态） |

如果 MRS 不在默认路径，用 `-MrsRoot D:\...\MounRiver_Studio3` 或给 make 传
`TOOLCHAIN=<工具链前缀>`。

> 不要定义 `DEBUG`：`PRINT()` 会走 UART1（PA9），与手柄串口协议冲突。

---

## 5. 烧录

1. 用 WCH-Link / WCH-LinkE 连接调试口；
2. MounRiver Studio 直接 Download，或用 **WCH-LinkUtility** 选择 hex 下载；
3. 用 WCH-LinkUtility 的“配置”页把 `CFG_DEBUG_EN` 设为 **0**（量产建议）。

---

## 6. 使用

| 操作 | 效果 |
|---|---|
| 上电 | PB23 红灯常亮，串口先发一帧 `00 00` |
| 插 USB | 免驱识别为 “HID-compliant game controller”，输入优先走 USB |
| BLE | 搜 “ESP-Mosaico MagPad” 配对，断开后自动重新广播 |
| 按住 SELECT 5 秒 | PB14 拉低：电池供电直接断电，USB 供电约 0.5s 后复位 |
| SELECT + START 3 秒 | 清除所有配对 |
| 15 分钟无按键 | PB14 拉低进入 Shutdown，按 START 唤醒并复位 |
| 电池 < 3.3V | 红灯 0.25Hz 闪烁（2s 亮 / 2s 灭） |
| 电池 < 3.0V | 串口 `A5 5B` 告警 + BLE 电量 5%，10 秒后下电 |

---

## 7. 串口协议

**19200 8N1**，单向发送。按键帧：

```
A5 5A <mask_L> <mask_H> <xor>
```

低电告警帧：

```
A5 5B <mv_L> <mv_H> <xor>
```

两种帧头不同，旧的上位机按 `A5 5A` 解析时会自动丢弃告警帧。
完整的掩码表、示例帧和可直接抄用的 C 解析代码见
**[docs/protocol.md](docs/protocol.md)**。

---

## 8. USB / BLE HID

7 字节报表（USB 不带 Report ID；BLE Report ID = 1）：

| 字节 | 内容 |
|---|---|
| 0 | X 左摇杆（0..255，128 中位，由 D-Pad 驱动） |
| 1 | Y 左摇杆 |
| 2 | Rx 右摇杆（固定 128） |
| 3 | Ry 右摇杆（固定 128） |
| 4 | hat：0 上 / 2 右 / 4 下 / 6 左，8 = 中位 |
| 5 | 按钮 1..8：A B X Y LB RB LT RT |
| 6 | 按钮 9..10：Back Start |

| 按钮 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| 功能 | A | B | X | Y | LB | RB | LT | RT | Back | Start |
| 来源 | K7 | K8 | K6 | K5 | — | — | K11 | K12 | K9 | K10 |

BLE 另提供标准电池服务：`0x180F` / `0x2A19`（Read + Notify），正常 100%，低电 5%。

---

## 9. 参数配置

| 想改什么 | 位置 |
|---|---|
| 按键引脚 / 极性 / 掩码 | `src/board.h` → `BOARD_KEY_LIST` |
| 串口波特率 | `src/board.h` → `BOARD_UART_BAUDRATE` |
| 消抖次数 | `src/keys.c` → `KEY_DEBOUNCE_SAMPLES` |
| 扫描周期 / HID 重发 | `src/main.c` → `PAD_SCAN_PERIOD_MS`、`PAD_RESEND_TICKS` |
| 长按关机 | `src/main.c` → `PAD_POWEROFF_KEY_MASK`、`PAD_POWEROFF_MS` |
| 空闲关机 | `src/main.c` → `PAD_IDLE_MS`（0 = 关闭） |
| 电池阈值 / 关断时间 | `src/main.c` → `PAD_BAT_LOW_MV`、`PAD_BAT_OFF_CHECKS` |
| BLE 电量两档 | `src/main.c` → `PAD_BAS_LEVEL_OK` / `PAD_BAS_LEVEL_LOW` |
| 指示灯阈值 | `src/main.c` → `PAD_LED_BLINK_MV` |
| HID 报表 / VID PID | `src/pad_hid.c`、`src/usb_hid.c` |
| 电池换算 | `src/battery.c` → `BAT_VREF_MV` |

---

## 10. 已知限制

1. **内部 VBAT 通道测的是 VDD33**，不是电池正极。LDO 稳压时读到 3.3V；只有电池
   跌到 LDO 压差以下读数才跟随电池，因此 3.0V 触发点实际约为 **3.0V + LDO 压差**。
   本检测的用途是区分**满电 / 亏电两档**（对应 BLE 电池服务的 100% / 5%），
   **不是电量百分比计**，不要用它推算剩余电量。
2. `Vref` 是芯片内部 1.05V（±1.5%），电池电压有几十 mV 误差；可用诊断固件的
   `BAT=` 读数与万用表对比后修正 `BAT_VREF_MV`。
3. HID 报表由 D-Pad 驱动左摇杆（参考固件行为），可用 `PAD_DPAD_DRIVES_STICK=0` 关闭。
4. 串口只发不收。
5. 未做低功耗睡眠（`HAL_SLEEP=FALSE`），常亮运行；空闲关机靠 Shutdown。

---

## 11. 许可与第三方

| 范围 | 许可 | 文件 |
|---|---|---|
| 应用层代码（`src/` 下除 WCH 派生文件外的源码、`scripts/`、`docs/`） | **MIT** | [`LICENSE`](LICENSE) |
| `src/hiddev.c/.h`、`src/devinfoservice.c/.h`、`sdk/ch59x/`（含 `libCH59xBLE.a`、`libISP592.a`） | WCH CH592 EVT SDK 原始条款 | [`THIRD-PARTY.md`](THIRD-PARTY.md) |

`src/` 下 `hiddev.c/.h`、`devinfoservice.c/.h` 以及 `sdk/ch59x/` 来自
**WCH（南京沁恒）CH592 EVT SDK**，版权归 WCH 所有，保留原始文件头；
`libCH59xBLE.a`、`libISP592.a` 为 WCH 提供的二进制库。

其余应用层代码为本项目原创。**MIT 不覆盖 WCH 的部分**，使用/分发前请遵守 WCH SDK 的授权条款。
