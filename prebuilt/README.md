# 预编译固件

编译环境：`riscv-none-embed-gcc 8.2.0`（MounRiver Studio 自带）+ `scripts/build.ps1`，**零告警**。

| 文件 | 大小 | SHA-256 | 说明 |
|---|---:|---|---|
| `PAD_CH592.hex` | 440,197 B | `cfe766eb4d100e9fe5ce6bd68ab4cfc248ab8c2a0ba9c951d2db8bdb3d87e534` | 正常手柄固件（USB HID + BLE HID + 串口 + 电池管理 + 指示灯） |
| `PAD_DIAG.hex` | 17,859 B | `c8beee3fc39b477a33bf42147e8d645875db00be2a04d35966c09bee586b2b41` | 诊断固件：USB CDC 虚拟串口，打印启动状态、按键掩码、PB14、电池电压 |

> **烧录后自检**：手机蓝牙应能搜到 **`ESP-Mosaico MagPad`**；USB 侧设备管理器里
> 制造商为 `ESP-Mosaico`、产品为 `ESP-Mosaico MagPad`。
> 名称不对 = 烧的还是旧 hex，请重新跑 `scripts/build.ps1`。

## 烧录

用 WCH-Link / WCH-LinkE + WCH-LinkUtility（或 MounRiver Studio Download）选择
hex 文件下载。

## 注意

- 这些 hex 由本仓库当前源码编译生成，改动源码后请重新 `scripts/build.ps1`；
- 诊断固件只做 CDC，不初始化 BLE/HID，仅用于排查；
- 首次烧录后建议用 WCH-LinkUtility 把 `CFG_DEBUG_EN` 配成 0（释放 PB14/PB15）。
