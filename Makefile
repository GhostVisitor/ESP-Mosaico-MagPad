# =============================================================================
# ESP-Mosaico 磁吸手柄（CH592F 三模）— 命令行构建
# -----------------------------------------------------------------------------
# 默认使用 MounRiver Studio 3 自带工具链，无需改 PATH：
#     make                             # 生成 build/PAD_CH592.elf/.hex/.siz/.map
#     make clean
#     make TOOLCHAIN=".../riscv-none-embed-"     # 指定工具链前缀
#     make TARGET=build/PAD_DIAG OBJ_DIR=build/obj_diag EXTRA_CFLAGS=-DPAD_DIAG_CDC
#
# 工具链下载：MounRiver Studio 3（内置 riscv-none-embed-gcc 8.2.0）
# =============================================================================

MRS_ROOT ?= C:/MounRiver/MounRiver_Studio3/resources/app/resources/win32
TOOLCHAIN ?= $(MRS_ROOT)/components/WCH/Toolchain/RISC-V Embedded GCC/bin/riscv-none-embed-

CC      := "$(TOOLCHAIN)gcc"
OBJCOPY := "$(TOOLCHAIN)objcopy"
OBJDUMP := "$(TOOLCHAIN)objdump"
SIZE    := "$(TOOLCHAIN)size"

VENDOR := sdk/ch59x
SRC    := src
BUILD  := build
OBJ_DIR ?= $(BUILD)/obj
TARGET ?= $(BUILD)/PAD_CH592
EXTRA_CFLAGS ?=

.DEFAULT_GOAL := all

LDSCRIPT ?= $(VENDOR)/SRC/Ld/Link.ld

ifeq ($(OS),Windows_NT)
  MKDIR_P = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
  RMDIR_P = if exist "$(subst /,\,$(1))" rd /s /q "$(subst /,\,$(1))"
else
  MKDIR_P = mkdir -p "$(1)"
  RMDIR_P = rm -rf "$(1)"
endif

# 注意：不要定义 DEBUG。DEBUG 会把 PRINT() 映射到 printf，而 printf 使用
# UART1(PA9)，与手柄的串口协议共用同一引脚，会破坏数据帧。
CFLAGS := -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 \
          -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char \
          -ffunction-sections -fdata-sections -fno-common -g \
          -DBLE_MEMHEAP_SIZE=6144 \
          $(EXTRA_CFLAGS) \
          -I$(SRC) \
          -I$(VENDOR)/HAL/include \
          -I$(VENDOR)/LIB \
          -I$(VENDOR)/SRC/RVMSIS \
          -I$(VENDOR)/SRC/StdPeriphDriver/inc \
          -I$(VENDOR)/SRC/Startup \
          -std=gnu99

LDFLAGS := -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 \
           -mno-save-restore -T $(LDSCRIPT) -nostartfiles \
           -Xlinker --gc-sections \
           -L$(VENDOR)/LIB -L$(VENDOR)/SRC/StdPeriphDriver \
           -Xlinker --print-memory-usage -Wl,-Map,$(TARGET).map \
           --specs=nano.specs --specs=nosys.specs

LIBS := -lISP592 -lCH59xBLE -lm

C_SRCS := $(wildcard $(SRC)/*.c) \
          $(wildcard $(VENDOR)/HAL/*.c) \
          $(wildcard $(VENDOR)/SRC/StdPeriphDriver/*.c) \
          $(VENDOR)/SRC/RVMSIS/core_riscv.c

S_SRCS := $(VENDOR)/SRC/Startup/startup_CH592.S \
          $(VENDOR)/LIB/ble_task_scheduler.S

# 目标文件平铺到 build/obj
BASENAMES := $(notdir $(C_SRCS) $(S_SRCS))
ifneq ($(words $(BASENAMES)),$(words $(sort $(BASENAMES))))
$(error 源文件基名冲突，请调整 build/obj 规则：$(sort $(BASENAMES)))
endif

OBJS := $(addprefix $(OBJ_DIR)/,$(patsubst %.c,%.o,$(patsubst %.S,%.o,$(BASENAMES))))

define RULE_C
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $$@ $$<
endef

define RULE_S
$(OBJ_DIR)/$(notdir $(1:.S=.o)): $(1) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $$@ $$<
endef

$(foreach s,$(C_SRCS),$(eval $(call RULE_C,$(s))))
$(foreach s,$(S_SRCS),$(eval $(call RULE_S,$(s))))

$(OBJ_DIR):
	@$(call MKDIR_P,$(OBJ_DIR))

.PHONY: all clean size

all: $(TARGET).elf $(TARGET).hex $(TARGET).siz

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(TARGET).siz: $(TARGET).elf
	$(SIZE) --format=berkeley $< > $@

size: $(TARGET).elf
	$(SIZE) --format=berkeley $<

clean:
	@$(call RMDIR_P,$(BUILD))

-include $(OBJS:.o=.d)
