# 硬件资料

把原理图、PCB、BOM、外壳等文件放到这个目录，例如：

```
hardware/
├── schematic/            # 原理图 PDF / 源文件
├── pcb/                  # Gerber、PCB 工程
├── bom/                  # BOM（xlsx / csv）
├── enclosure/            # 外壳 3D（STEP / STL / 工程文件）
└── photos/               # 实物照片
```

## 设计要点（与固件相关）

- **主控**：CH592F（QFN28），32MHz 晶振接 18/19 脚，蓝牙天线接 21 脚。
- **供电**：单节锂电池 → 3.3V LDO → VDD33；`PB14` 接 LDO 使能（高有效），
  固件上电后第一件事就是拉高它做自保持。
- **外部 EN**：本板另有一路外部 EN 参与电源控制。外部电源在线时，固件拉低
  PB14 后 MCU 仍带电，可进入 Shutdown；不在线时拉低 PB14 即断电。
- **按键**：K1~K9、K11、K12 为按下接地（低有效，内部上拉）；
  **K10（PB15）为按下接高**（内部下拉）——如果改硬件，记得同步改
  `src/board.h` 里的 `active_high` 字段。
- **PB15 兼作关机唤醒源**：Shutdown 时配置为内部下拉 + 上升沿唤醒。
- **PB23**：红色 LED，高电平点亮，串限流电阻到地。
- **调试口**：PB14/PB15 复位后默认是 TIO/TCK。量产时用 WCH-LinkUtility 把
  `CFG_DEBUG_EN` 烧成 0，否则这两个脚不能当 GPIO 用。

## 参考引脚表

见 README 的“硬件 → 引脚分配”。
