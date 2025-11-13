# RTSP视频流客户端 (STM32MP157)

这是一个基于V4L2的RTSP视频流客户端程序，运行在STM32MP157开发板上，可以从本地摄像头采集视频并通过RTSP协议发送到远程服务器。

## 功能特性

- 使用V4L2从摄像头采集YUYV格式视频
- 将YUYV转换为H264编码（使用x264库）
- 作为RTSP客户端连接到远程RTSP服务器
- 通过TCP发送RTSP控制协议
- 通过UDP发送RTP视频流

## 交叉编译环境

### 工具链
- **交叉编译器**: `arm-none-linux-gnueabihf-gcc`
- **目标平台**: STM32MP157 (ARM Cortex-A7)

### 依赖库
- libx264 (需要交叉编译的ARM版本)

## 编译

### 1. 交叉编译x264库（如果还没有）

```bash
# 下载x264源码
git clone https://code.videolan.org/videolan/x264.git
cd x264

# 配置交叉编译
./configure --host=arm-none-linux-gnueabihf \
            --cross-prefix=arm-none-linux-gnueabihf- \
            --prefix=/opt/stm32mp1/x264 \
            --enable-static \
            --disable-cli

# 编译和安装
make -j4
make install
```

### 2. 编译RTSP客户端

```bash
cd camera

# 如果x264安装在默认路径，直接编译
make

# 如果x264安装在其他路径，指定安装目录
make X264_INSTALL_DIR=/opt/stm32mp1/x264

# 查看编译信息
make info
```

编译成功后，会生成 `rtsp_client` 可执行文件。

## 部署到STM32MP157

### 1. 传输文件到开发板

```bash
# 使用scp传输（假设开发板IP为192.168.1.100）
scp rtsp_client root@192.168.1.100:/usr/bin/

# 或者使用其他方式（NFS、TFTP等）
```

### 2. 设置可执行权限

```bash
# 在开发板上执行
chmod +x /usr/bin/rtsp_client
```

## 使用方法

### 基本用法

```bash
rtsp_client -s <服务器IP> [选项]
```

### 命令行选项

```
-d, --device DEVICE     摄像头设备 (默认: /dev/video0)
-s, --server IP         RTSP服务器IP地址（必需）
-p, --port PORT         RTSP服务器端口 (默认: 554)
-u, --url URL           RTSP URL路径 (默认: /stream)
-w, --width WIDTH       视频宽度 (默认: 640)
-h, --height HEIGHT     视频高度 (默认: 480)
-r, --rtp-port PORT     本地RTP端口 (默认: 5004)
-?, --help              显示帮助信息
```

### 使用示例

```bash
# 连接到远程服务器，使用默认参数
rtsp_client -s 192.168.1.50

# 指定分辨率和摄像头设备
rtsp_client -s 192.168.1.50 -d /dev/video1 -w 1280 -h 720

# 指定自定义RTSP URL和端口
rtsp_client -s 192.168.1.50 -p 8554 -u /live/stream
```

## 程序流程

1. **连接RTSP服务器** - 通过TCP连接到指定的RTSP服务器
2. **RTSP握手**:
   - OPTIONS - 查询服务器支持的方法
   - DESCRIBE - 获取媒体描述
   - SETUP - 设置传输参数（RTP端口）
   - PLAY - 开始播放
3. **视频采集** - 从V4L2摄像头采集YUYV视频帧
4. **H264编码** - 将YUYV转换为H264格式
5. **RTP传输** - 通过UDP发送RTP包到服务器

## 项目结构

- `tcp.h/tcp.c` - TCP socket功能，用于RTSP协议
- `udp.h/udp.c` - UDP socket功能，用于RTP传输
- `rtsp.h/rtsp.c` - RTSP协议处理（客户端模式）
- `v4l2.h/v4l2.c` - V4L2摄像头接口和H264编码
- `rtsp_client.c` - 客户端主程序

## 注意事项

1. **摄像头权限**: 确保程序有权限访问摄像头设备（可能需要root权限或加入video组）
2. **网络连接**: 确保开发板与RTSP服务器网络连通
3. **防火墙**: 确保防火墙允许TCP和UDP通信
4. **x264库**: 必须使用ARM版本的x264库，不能使用x86版本
5. **性能**: H264编码需要一定CPU资源，根据实际性能调整分辨率和帧率

## 故障排查

### 编译错误：找不到x264库
- 确认已交叉编译x264库
- 检查 `X264_INSTALL_DIR` 路径是否正确
- 运行 `make info` 查看编译信息

### 运行时错误：无法打开摄像头
- 检查设备路径：`ls -l /dev/video*`
- 检查权限：可能需要使用 `sudo` 或加入 `video` 组
```bash
# 添加当前用户到video组
sudo usermod -a -G video $USER
```

### 无法连接到RTSP服务器
- 检查网络连接：`ping <服务器IP>`
- 检查端口是否开放：`telnet <服务器IP> 554`
- 检查防火墙设置

### 视频编码失败
- 确认x264库已正确链接
- 检查摄像头是否正常工作
- 查看程序输出的错误信息

### 性能问题
- 降低视频分辨率（使用 `-w` 和 `-h` 参数）
- 检查CPU使用率：`top` 或 `htop`
- 考虑使用硬件编码（如果硬件支持）

## 开发板信息

- **平台**: STM32MP157
- **架构**: ARM Cortex-A7
- **工具链**: arm-none-linux-gnueabihf-gcc

## 许可证

本代码仅供学习和研究使用。
