# ARM交叉编译工具链
# X264_INSTALL_DIR = /home/jetson/develop/thirdlib/x264
# JPEG_INSTALL_DIR = /home/jetson/develop/thirdlib/libjpeg-turbo
# CROSS_COMPILE = arm-none-linux-gnueabihf-
X264_INSTALL_DIR = /home/ptx/Develop/thirdlib/x264-aarch64
JPEG_INSTALL_DIR = /home/ptx/Develop/thirdlib/libjpeg-turbo-aarch64
CROSS_COMPILE = aarch64-buildroot-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -g -O0 \
		 -I$(X264_INSTALL_DIR)/include \
		 -I$(JPEG_INSTALL_DIR)/include \
		 -fPIC \
		 -U_FORTIFY_SOURCE \
		 -fsanitize=address \
		 -fno-omit-frame-pointer
LDFLAGS = -L$(X264_INSTALL_DIR)/lib \
		  -L$(JPEG_INSTALL_DIR)/lib \
# 		  -Wl,-rpath=/home/jetson/develop/thirdlib
LIBS = -lx264 -lturbojpeg -ljpeg -lpthread -lm -fsanitize=address
# 源文件
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
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

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
