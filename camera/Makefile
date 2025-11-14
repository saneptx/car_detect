# ARM交叉编译工具链
X264_INSTALL_DIR = /home/ptx/x264_arm
CROSS_COMPILE = arm-none-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O2 -I$(X264_INSTALL_DIR)/include
LDFLAGS = -L$(X264_INSTALL_DIR)/lib -lx264 -lpthread -lm

# 检查是否有x264库
X264_LIB = $(X264_INSTALL_DIR)/lib/libx264.so
ifeq ($(wildcard $(X264_LIB)),)
    $(warning ⚠️ 未找到x264库，H264编码功能将不可用)
    $(warning 💡 请设置X264_INSTALL_DIR指向交叉编译x264的安装目录)
else
    CFLAGS += -DHAVE_X264
endif

# 源文件
TCP_SRC = tcp.c
UDP_SRC = udp.c
RTSP_SRC = rtsp.c
V4L2_SRC = v4l2.c
CLIENT_SRC = rtsp_client.c

# 目标文件目录
BUILD_DIR = build

# 目标文件
OBJS = $(addprefix $(BUILD_DIR)/, $(TCP_SRC:.c=.o) $(UDP_SRC:.c=.o) $(RTSP_SRC:.c=.o) $(V4L2_SRC:.c=.o) $(CLIENT_SRC:.c=.o))

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

info:
	@echo "交叉编译器: $(CC)"
	@echo "x264库路径: $(X264_LIB)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
