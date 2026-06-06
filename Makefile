#
# Makefile for LVGL project with modular user code (all in usrCode/)
#

CC = arm-linux-gcc
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}
CFLAGS ?= -O3 -g0 -I$(LVGL_DIR)/ -I$(LVGL_DIR)/usrCode -Wall -std=gnu99
LDFLAGS ?= -lm -lpthread
BIN = demo

# 收集 usrCode 下所有 .c 文件（包括 main.c 和各模块）
USRCODESRC = ${shell find $(LVGL_DIR)/usrCode -name '*.c'}

# 包含 LVGL 和 lv_drivers 的 makefile
include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

# 可选额外源文件（如鼠标光标）
CSRCS += $(LVGL_DIR)/mouse_cursor_icon.c

OBJEXT ?= .o

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
USRCODEOBJ = $(USRCODESRC:.c=$(OBJEXT))

# 不再单独定义 MAINOBJ

all: default

%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: $(AOBJS) $(COBJS) $(USRCODEOBJ)
	$(CC) -o $(BIN) $(AOBJS) $(COBJS) $(USRCODEOBJ) $(LDFLAGS)

clean:
	rm -f $(BIN) $(AOBJS) $(COBJS) $(USRCODEOBJ)

send:
	scp demo root@192.168.137.226:~/yjr/program2

# 可选：显示收集到的用户源文件列表（调试用）
show:
	@echo "USRCODESRC = $(USRCODESRC)"