CXX := g++
CXXFLAGS := -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter -Ireactor -Imedia -g -O0 -fsanitize=address -fno-omit-frame-pointer
LDFLAGS := -llog4cpp -lavformat -lavcodec -lavutil -lswscale -L/usr/lib/x86_64-linux-gnu -lSDL2 -lpthread -fsanitize=address

# 自动收集源码
REACTOR_SRCS := $(wildcard reactor/*.cc)
MEDIA_SRCS := $(wildcard media/*.cc)
SERVER_SRC := RtspServer.cc
IKCP_SRC := media/ikcp.c
SRCS := $(REACTOR_SRCS) $(MEDIA_SRCS) $(SERVER_SRC) $(IKCP_SRC)

# 生成对应的对象文件到 build 目录
BUILD_DIR := build
OBJS := $(patsubst %.cc,$(BUILD_DIR)/%.o,$(SRCS))

# 目标可执行文件
TARGET := rtsp_server

.PHONY: all clean dirs

all: dirs $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) -g -O0
 
# 规则：将任意子目录下的 .cc 编译为 build/同路径/.o
$(BUILD_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 创建 build 目录
dirs:
	@mkdir -p $(BUILD_DIR)/reactor $(BUILD_DIR)/media

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
