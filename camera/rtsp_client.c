#include "rtsp.h"
#include "v4l2.h"
#include <pthread.h>
#include <signal.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <sys/select.h>   // 引入 select
#include <sys/time.h>     // 引入 gettimeofday
#include <sys/socket.h>   // 引入 socket 相关的
#include <fcntl.h>        // 引入 fcntl
#include <errno.h>        // 引入 errno
#include "log.h"
#include "kcp.h"

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS 30
#define DEFAULT_RTP_PORT 5004
#define DEFAULT_RTCP_PORT 5005
#define MAX_BUFFER_SIZE 2048 // UDP/KCP 接收缓冲区大小

/* 设置文件描述符为非阻塞模式 */
static int set_socket_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL) failed");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL, O_NONBLOCK) failed");
        return -1;
    }
    return 0;
}

static int running = 1;
static h264_encoder_t encoder;
static rtsp_session_t session;
FILE *h264_file;
/* 信号处理 */
void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static inline int is_start_code3(const uint8_t *p){ return p[0]==0 && p[1]==0 && p[2]==1; }
static inline int is_start_code4(const uint8_t *p){ return p[0]==0 && p[1]==0 && p[2]==0 && p[3]==1; }

typedef struct { const uint8_t* ptr; size_t len; } nalu_view_t;

static int split_annexb_nalus(const uint8_t *buf, int len, nalu_view_t *out, int max_nalus) {
    int cnt = 0, i = 0, start = -1;
    while (i + 3 < len) {
        int sc = 0;
        if (is_start_code4(buf + i)) { sc = 4; }
        else if (is_start_code3(buf + i)) { sc = 3; }
        if (sc) {
            if (start >= 0 && cnt < max_nalus) {
                out[cnt].ptr = buf + start;
                out[cnt].len = i - start;
                cnt++;
            }
            i += sc;
            start = i;
        } else {
            i++;
        }
    }
    if (start >= 0 && start < len && cnt < max_nalus) {
        out[cnt].ptr = buf + start;
        out[cnt].len = len - start;
        cnt++;
    }
    return cnt;
}

/* 视频流发送线程 */
void *video_stream_thread(void *arg) {
    rtsp_session_t *sess = (rtsp_session_t *)arg;
    struct v4l2_buffer buf;
    unsigned char *mjpeg_data;
    size_t h264_buf_size = (size_t)encoder.width * encoder.height * 2;
    unsigned char *h264_data = NULL;
    int h264_len = 0;

    uint32_t timestamp = 0;
    const uint32_t timestamp_increment = 90000 / DEFAULT_FPS;  /* 90kHz时钟 */
    h264_data = (unsigned char *)malloc(h264_buf_size);
    if (!h264_data) {
        fprintf(stderr, "无法为H264输出分配内存: %zu字节\n", h264_buf_size);
        return NULL;
    }
    h264_file = fopen("original.h264", "wb");
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    printf("视频流线程启动\n");
    while (running && sess->state == RTSP_STATE_PLAYING) {
        /* 从摄像头获取一帧 */
        if (v4l2_dqbuf(&buf) < 0) {//出队 
            usleep(10000);
            continue;
        }
        mjpeg_data = (unsigned char *)buf_infos[buf.index].start;
        size_t mjpeg_size = buf.bytesused;
        /* 解码MJPEG并编码为H264 */
        if (mjpeg_to_h264(&encoder, mjpeg_data, mjpeg_size, h264_data,
                          h264_buf_size, &h264_len) == 0 && h264_len > 0) {
            if(h264_file){
                fwrite(h264_data, 1, h264_len, h264_file);
            }
            /* 发送RTP包到服务器 */
            nalu_view_t nalus[32];
            int n = split_annexb_nalus(h264_data, h264_len, nalus, 32);
            for (int k = 0; k < n; ++k) {
                const uint8_t *nalu = nalus[k].ptr;//待发送nalu
                size_t nalu_size = nalus[k].len;//待发送nalu长度
                // 2) 逐 NALU 发送
                rtp_send_h264(sess, &timestamp, nalu, nalu_size);
            }
            // 这一帧的所有 NAL 都发完了，再推进一次时间戳
            timestamp += timestamp_increment;      
        }
        // usleep(90000/DEFAULT_FPS);
        /* 将缓冲区放回队列 */
        v4l2_qbuf(&buf);
    }
    printf("视频流线程退出\n");
    free(h264_data);
    return NULL;
}

void print_usage(const char *prog_name) {
    printf("用法: %s [选项]\n", prog_name);
    printf("选项:\n");
    printf("  -d, --device DEVICE     摄像头设备 (默认: /dev/video0)\n");
    printf("  -s, --server IP         RTSP服务器IP地址\n");
    printf("  -p, --port PORT         RTSP服务器端口 (默认: 8554)\n");
    printf("  -u, --url URL           RTSP URL路径 (默认: /stream)\n");
    printf("  -w, --width WIDTH       视频宽度 (默认: 640)\n");
    printf("  -h, --height HEIGHT     视频高度 (默认: 480)\n");
    printf("  -r, --rtp-port PORT     本地RTP端口 (默认: 5004)\n");
    printf("  -t, --tcp or udp        默认udp\n");
    printf("  -?, --help              显示帮助信息\n");
}

int main(int argc, char *argv[]) {
    const char *video_device = "/dev/video0";
    const char *server_ip = NULL;
    char transType[8] = "udp";
    int server_port = 8554;
    const char *url = "/stream";
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    uint16_t rtp_port = DEFAULT_RTP_PORT;//默认端口号
    uint16_t rtcp_port = DEFAULT_RTCP_PORT;
    
    char rtsp_url[256];
    char transport[256];
    char response[RTSP_BUFFER_SIZE];
    char sdp[512];
    pthread_t video_thread;
    
    /* 命令行参数解析 */
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"server", required_argument, 0, 's'},
        {"port", required_argument, 0, 'p'},
        {"url", required_argument, 0, 'u'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"rtp-port", required_argument, 0, 'r'},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "d:s:p:u:w:h:r:t:?", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                video_device = optarg;
                break;
            case 's':
                server_ip = optarg;
                break;
            case 'p':
                server_port = atoi(optarg);
                break;
            case 'u':
                url = optarg;
                break;
            case 'w':
                width = atoi(optarg);
                break;
            case 'h':
                height = atoi(optarg);
                break;
            case 'r':
                rtp_port = atoi(optarg);//可以自行更改端口号
                rtcp_port = rtp_port + 1;
                break;
            case 't':
                snprintf(transType, sizeof(transType), "%s", optarg);
                break;
            case '?':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }
    
    if (!server_ip) {
        fprintf(stderr, "错误: 必须指定RTSP服务器IP地址 (-s 或 --server)\n");
        print_usage(argv[0]);
        return -1;
    }
    
    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* 初始化RTSP会话 */
    // rtsp_session_init(&session);
    session.rtp_ssrc = ((uint32_t)rand() << 16) ^ rand();
    // pthread_mutex_init(&session.rtp_send_mtx, NULL);
    session.rtp_port = rtp_port;
    session.rtcp_port = rtcp_port;
    
    /* 连接到RTSP服务器 */
    printf("连接到RTSP服务器: %s:%d\n", server_ip, server_port);
    if (tcp_client_connect(&session.client, server_ip, server_port) < 0) {
        fprintf(stderr, "无法连接到RTSP服务器\n");
        return -1;
    }
    
    /* 初始化摄像头 */
    printf("初始化摄像头: %s\n", video_device);
    if (v4l2_dev_init(video_device) < 0) {
        fprintf(stderr, "初始化摄像头失败\n");
        tcp_close_client(&session.client);
        return -1;
    }
    
    /* 设置视频格式 */
    if (v4l2_set_format(width, height) < 0) {
        fprintf(stderr, "设置视频格式失败\n");
        v4l2_cleanup();
        tcp_close_client(&session.client);
        return -1;
    }

    /* 初始化缓冲区 */
    if (v4l2_init_buffer() < 0) {
        fprintf(stderr, "初始化缓冲区失败\n");
        v4l2_cleanup();
        tcp_close_client(&session.client);
        return -1;
    }
    
    /* 启动视频流 */
    if (v4l2_stream_on() < 0) {
        fprintf(stderr, "启动视频流失败\n");
        v4l2_cleanup();
        tcp_close_client(&session.client);
        return -1;
    }
    
    /* 初始化H264编码器 */
    if (h264_encoder_init(&encoder, width, height, DEFAULT_FPS) < 0) {
        fprintf(stderr, "初始化H264编码器失败\n");
        fprintf(stderr, "提示: 请安装libx264-dev，并在编译时定义HAVE_X264\n");
        v4l2_stream_off();
        v4l2_cleanup();
        tcp_close_client(&session.client);
        return -1;
    }
    
    /* 构建RTSP URL */
    snprintf(rtsp_url, sizeof(rtsp_url), "rtsp://%s:%d%s", server_ip, server_port, url);
    
    /* RTSP握手流程 */
    printf("\n=== RTSP握手流程 ===\n");

    /* 1.OPTIONS */
    printf("\n[1] 发送OPTIONS请求...\n");
    if (rtsp_client_options(&session, rtsp_url) < 0){
        fprintf(stderr, "发送OPTIONS请求失败\n");
        goto cleanup;
    }
    if (rtsp_client_read_response(&session, response, sizeof(response)) <= 0) {
        fprintf(stderr, "接收OPTIONS响应失败\n");
        goto cleanup;
    }
    
    /* 2. ANNOUNCE（推流场景，携带SDP） */
    printf("\n[1] 发送ANNOUNCE请求...\n");
    snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=RTSP Push\r\n"
        "t=0 0\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=control:trackID=0\r\n");
    if (rtsp_client_announce(&session, rtsp_url, sdp) < 0) {
        fprintf(stderr, "发送ANNOUNCE请求失败\n");
        goto cleanup;
    }
    if (rtsp_client_read_response(&session, response, sizeof(response)) <= 0) {
        fprintf(stderr, "接收ANNOUNCE响应失败\n");
        goto cleanup;
    }
    
    /* 3. SETUP */
    printf("\n[3] 发送SETUP请求...\n");
    if(strcmp(transType,"tcp") == 0){//使用tcp传输
        snprintf(transport, sizeof(transport),"RTP/AVP/TCP;unicast;interleaved=0-1");
    }else{
        snprintf(transport, sizeof(transport),"RTP/AVP/UDP;unicast;client_port=%d-%d", rtp_port, rtcp_port);
    }
    
    if (rtsp_client_setup(&session, rtsp_url, transport) < 0) {//使用tcp传输
        fprintf(stderr, "发送SETUP请求失败\n");
        goto cleanup;
    }
    if (rtsp_client_read_response(&session, response, sizeof(response)) <= 0) {
        fprintf(stderr, "接收SETUP响应失败\n");
        goto cleanup;
    }
    
    /* 解析SETUP响应，获取服务器端口和Session ID以及kcpId*/
    if (rtsp_parse_setup_response(response, &session) < 0) {
        fprintf(stderr, "解析SETUP响应失败\n");
        goto cleanup;
    }
    printf("服务器RTP端口: %d, RTCP端口: %d\n", session.server_rtp_port, session.server_rtcp_port);
    printf("Session ID: %s\n", session.session_id);

    /* 初始化UDP socket用于RTP传输（发送到服务器） */
    if (udp_init(&session.rtp_socket, server_ip, session.server_rtp_port) < 0) {
        fprintf(stderr, "初始化UDP socket失败\n");
        goto cleanup;
    }
    
    session.state = RTSP_STATE_READY;
    
    session.kcp = kcp_init(&session.rtp_socket);
    /* 4. RECORD（开始推流） */
    printf("\n[4] 发送RECORD请求...\n");
    
    if (rtsp_client_record(&session, rtsp_url) < 0) {
        fprintf(stderr, "发送RECORD请求失败\n");
        goto cleanup;
    }
    if (rtsp_client_read_response(&session, response, sizeof(response)) <= 0) {
        fprintf(stderr, "接收RECORD响应失败\n");
        goto cleanup;
    }
    session.state = RTSP_STATE_PLAYING; /* 用PLAYING标识推流进行中 */
    printf("\n=== 开始发送视频流（RECORD）===\n");
    // pthread_t tid;
    // pthread_create(&tid, NULL, kcp_timer_thread, (void*)session.kcp);//启动kcp定时器线程
    // pthread_detach(tid);
    /* 启动视频流发送线程 */
    if (pthread_create(&video_thread, NULL, video_stream_thread, &session) != 0) {
        fprintf(stderr, "创建视频流线程失败\n");
        goto cleanup;
    }
    pthread_detach(video_thread);
    
    // /* 主循环：保持连接并处理可能的RTSP消息 */
    // while (running) {
    //     /* 可以在这里处理服务器发来的RTSP消息（如TEARDOWN） */
    //     usleep(1000000);  /* 1秒检查一次 */
    // }
    int maxfd;
    fd_set read_fds;
    struct timeval tv;
    IUINT32 next_kcp_update_time;
    maxfd = session.client.fd;
    if (session.rtp_socket.fd > maxfd) {
        maxfd = session.rtp_socket.fd;
    }
    next_kcp_update_time = iclock();
    while(running){
        IUINT32 current_ms = iclock();
        long wait_ms;
        if (current_ms >= next_kcp_update_time) {
            // 时间已到或已过期，驱动 KCP
            ikcp_update(session.kcp, current_ms);
            // 计算下一次更新时间
            next_kcp_update_time = ikcp_check(session.kcp, current_ms);
        }
        if (next_kcp_update_time > current_ms) {
            wait_ms = (long)(next_kcp_update_time - current_ms);
            // 确保等待时间不超过 1 秒（防止意外长时间等待）
            if (wait_ms > 1000) wait_ms = 1000;
        } else {
            // 理论上不会发生，但作为保护，确保select不会阻塞
            wait_ms = 1; 
        }
        tv.tv_sec = wait_ms / 1000;
        tv.tv_usec = (wait_ms % 1000) * 1000;
        FD_ZERO(&read_fds);
        FD_SET(session.client.fd, &read_fds); // RTSP TCP
        FD_SET(session.rtp_socket.fd, &read_fds); // KCP UDP
        set_socket_nonblocking(session.rtp_socket.fd);
        int activity = select(maxfd + 1, &read_fds, NULL, NULL, &tv);
        
        if (activity < 0) {
            if (running) {
                perror("select error");
            }
            break;
        }
        
        if (activity == 0) {
            // 超时，继续循环，让 KCP 驱动逻辑在下一次循环中处理 ikcp_update
            continue;
        }

        // ----------------------------------------------------
        // 处理 I/O 事件
        // ----------------------------------------------------
        
        // A. RTSP TCP Socket (接收 TEARDOWN, PAUSE 等控制)
        if (FD_ISSET(session.client.fd, &read_fds)) {
            // 注意: 假设 rtsp_client_read_response 能处理非阻塞或 EAGAIN 的情况
            int len = rtsp_client_read_response(&session, response, sizeof(response));
            
            if (len > 0) {
                LOG_INFO("Received RTSP control message:\n%.*s", len, response);
                // 简单的退出逻辑：收到 TEARDOWN 或错误时停止
                if (strstr(response, "TEARDOWN") || strstr(response, "400")) {
                    LOG_WARNING("Server requested TEARDOWN or error. Stopping.");
                    running = 0;
                }
            } else if (len == 0) {
                LOG_ERROR("RTSP server closed the TCP connection (FIN). Stopping.");
                running = 0;
            } else {
                // 读取错误 (若非 EAGAIN/EWOULDBLOCK，则视为致命错误)
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("RTSP TCP read error");
                    running = 0;
                }
            }
        }

        // B. KCP UDP Socket (接收 ACK 或 RTCP)
        if (FD_ISSET(session.rtp_socket.fd, &read_fds)) {
            char udp_buffer[MAX_BUFFER_SIZE]; 
            ssize_t n;
            // 循环读取所有待处理的 UDP 包
            while (true) {
                n = udp_recv(&session.rtp_socket, udp_buffer, sizeof(udp_buffer));
                if (n > 0) {
                    // 正常接收数据，喂给 KCP
                    ikcp_input(session.kcp, udp_buffer, (int)n);
                    // LOG_DEBUG("Received UDP packet (len=%zd) and fed to KCP.", n);
                } else if (n == -1) {
                    // 区分 EAGAIN（无数据）和真错误
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 无数据，退出循环（无需打印错误）
                        break;
                    } else {
                        // 真错误，打印并处理
                        LOG_ERROR("UDP recv error: %s", strerror(errno));
                        break;
                    }
                } else { // n == 0，UDP 无连接，n=0 无意义
                    break;
                }
            }
        }
    }



    printf("\n正在关闭...\n");
    
cleanup:
    /* 5. TEARDOWN */
    if (session.state != RTSP_STATE_INIT) {
        rtsp_client_teardown(&session, rtsp_url);
        rtsp_client_read_response(&session, response, sizeof(response));
    }
    
    /* 等待视频流线程退出 */
    usleep(200000);
    fclose(h264_file);
    /* 清理资源 */
    h264_encoder_cleanup(&encoder);
    v4l2_stream_off();
    v4l2_cleanup();
    rtsp_session_cleanup(&session);
    ikcp_release(session.kcp);
    printf("程序退出\n");
    return 0;
}
