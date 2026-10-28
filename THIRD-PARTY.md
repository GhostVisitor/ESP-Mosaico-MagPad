# 第三方代码与许可

## WCH CH592 EVT SDK

以下内容来自南京沁恒微电子（WCH）发布的 **CH592 EVT** 软件开发包：

- `sdk/ch59x/HAL/` — 片内外设抽象层
- `sdk/ch59x/SRC/StdPeriphDriver/` — 标准外设驱动
- `sdk/ch59x/SRC/RVMSIS/` — RISC-V 内核支持
- `sdk/ch59x/SRC/Startup/` — 启动文件
- `sdk/ch59x/SRC/Ld/` — 链接脚本
- `sdk/ch59x/LIB/` — BLE 协议栈（`libCH59xBLE.a`）及其头文件、任务调度汇编

这些文件的版权归 WCH 所有，文件头保留原始声明。它们随 WCH 芯片一起提供，
使用与再分发请遵守 WCH 的授权条款（详见 WCH 官网与 EVT 包内说明）。

## HID 设备管理

`src/hiddev.c`、`src/hiddev.h`、`src/devinfoservice.c`、`src/devinfoservice.h`
同样来自 WCH EVT（HID_Keyboard 例程），本仓库仅做了裁剪（移除电池服务、
扫描参数服务等），版权归 WCH。

## 应用层代码

`src/` 下其余文件（main / board / keys / pad_hid / usb_hid / usb_cdc / diag /
pad_ble / hidpadservice / bas_service / battery / uart_link 及对应头文件）
为本项目原创。

## 许可范围（重要）

| 范围 | 许可 |
|---|---|
| **应用层代码**：`src/` 下除 `hiddev.*` / `devinfoservice.*` 外的全部源码与头文件，以及 `scripts/`、`docs/` | **MIT**，全文见 [`LICENSE`](LICENSE) |
| `sdk/ch59x/`、`src/hiddev.c/.h`、`src/devinfoservice.c/.h`（含 `libCH59xBLE.a`、`libISP592.a`） | **不在 MIT 范围内**，版权归 WCH，遵守其原始条款 |

`LICENSE` 只包含标准 MIT 正文（方便 GitHub 自动识别），**不表示 MIT 覆盖仓库里的 WCH 文件**。
