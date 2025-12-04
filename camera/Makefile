# ARM交叉编译工具链
X264_INSTALL_DIR = /home/jetson/develop/thirdlib/x264
JPEG_INSTALL_DIR = /home/jetson/develop/thirdlib/libjpeg-turbo
# CROSS_COMPILE = arm-none-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -g -O2 -I$(X264_INSTALL_DIR)/include -I$(JPEG_INSTALL_DIR)/include
LDFLAGS = -L$(X264_INSTALL_DIR)/lib -lx264 -lpthread -lm -L$(JPEG_INSTALL_DIR)/lib -lturbojpeg -ljpeg

# 源文件
# TCP_SRC = tcp.c
# UDP_SRC = udp.c
# RTSP_SRC = rtsp.c
# V4L2_SRC = v4l2.c
# CLIENT_SRC = rtsp_client.c
SRCS := $(wildcard *.c)
# 目标文件目录
BUILD_DIR := build

# 目标文件
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

# 可执行文件
TARGET = rtsp_client

.PHONY: all clean info

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)  # 确保build目录存在
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# info:
# 	@echo "交叉编译器: $(CC)"
# 	@echo "x264库路径: $(X264_LIB)"
# 	@echo "CFLAGS: $(CFLAGS)"
# 	@echo "LDFLAGS: $(LDFLAGS)"
