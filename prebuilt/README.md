# 预编译固件

| 文件 | 说明 |
|---|---|
| `PAD_CH592.hex` | 正常手柄固件（USB HID + BLE HID + 串口 + 电池管理 + 指示灯） |
| `PAD_DIAG.hex` | 诊断固件：USB CDC 虚拟串口，打印启动状态、按键掩码、PB14、电池电压 |

## 烧录

用 WCH-Link / WCH-LinkE + WCH-LinkUtility（或 MounRiver Studio Download）选择
hex 文件下载。

## 注意

- 这些 hex 由本仓库当前源码编译生成，改动源码后请重新 `scripts/build.ps1`；
- 诊断固件只做 CDC，不初始化 BLE/HID，仅用于排查；
- 首次烧录后建议用 WCH-LinkUtility 把 `CFG_DEBUG_EN` 配成 0（释放 PB14/PB15）。
