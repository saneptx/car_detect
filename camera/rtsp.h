#ifndef _RTSP_H_
#define _RTSP_H_

#include "tcp.h"
#include "udp.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include "kcp.h"

#define RTSP_BUFFER_SIZE 2048
#define MTU 1400
#define RTP_HEADER_SIZE 12
#define RTSP_MAX_URL 512

/* RTSP会话状态 */
typedef enum {
    RTSP_STATE_INIT,
    RTSP_STATE_READY,
    RTSP_STATE_PLAYING
} rtsp_state_t;

/* RTSP会话 */
typedef struct rtsp_session {
    tcp_client_t client;
    rtsp_state_t state;
    ikcpcb *kcp;
    char session_id[64];
    int cseq;
    //udp传输
    uint16_t rtp_port;
    uint16_t rtcp_port;
    uint16_t server_rtp_port;
    uint16_t server_rtcp_port;
    udp_socket_t rtp_socket;
    //tcp传输
    uint16_t rtpChannel;
    uint16_t rtcpChannel;
    uint32_t rtp_ssrc;
    atomic_uint_fast16_t rtp_seq;
    // pthread_mutex_t      rtp_send_mtx; // 可选：对 send 整体加锁
    uint32_t rtp_timestamp;
    /* 来自DESCRIBE/SDP解析的信息 */
    char content_base[RTSP_MAX_URL];
    char control_attr[256];
    char setup_url[RTSP_MAX_URL];
    int payload_type;   /* 如 96 */
    int clock_rate;     /* 如 90000 */
    /* SPS/PPS缓存，避免重复发送 */
    // uint8_t *sps;
    // size_t sps_size;
    // uint8_t *pps;
    // size_t pps_size;
    char transType[32];
} rtsp_session_t;


/* 初始化RTSP会话 */
int rtsp_session_init(rtsp_session_t *session);

/* 创建RTP包 */
void build_rtp_header(uint8_t *header, uint16_t seq, uint32_t timestamp,
    uint32_t ssrc, uint8_t payload_type, bool marker);

/* 发送RTP包（包含H264 NAL单元） */
int send_rtp_over_tcp(rtsp_session_t *sess, const uint8_t *rtp_data,
        size_t rtp_len, uint8_t channel);
void rtp_send_h264(rtsp_session_t *session, uint32_t *timestamp,
    const uint8_t *nalu, size_t nalu_size);
void send_h264_frame(rtsp_session_t *session, uint32_t *timestamp,
    const uint8_t *nalu, size_t nalu_size);
// void send_h264_frame_udp(rtsp_session_t *session, uint32_t *timestamp,
//     const uint8_t *nalu, size_t nalu_size);

/* 清理RTSP会话 */
void rtsp_session_cleanup(rtsp_session_t *session);

/* ========== RTSP客户端功能 ========== */

/* 发送RTSP OPTIONS请求 */
int rtsp_client_options(rtsp_session_t *session, const char *url);

/* 发送RTSP DESCRIBE请求 */
int rtsp_client_describe(rtsp_session_t *session, const char *url);

/* 发送RTSP SETUP请求 */
int rtsp_client_setup(rtsp_session_t *session, const char *url, const char *transport);


/* 发送RTSP TEARDOWN请求 */
int rtsp_client_teardown(rtsp_session_t *session, const char *url);

/* 接收RTSP响应 */
int rtsp_client_read_response(rtsp_session_t *session, char *response, int len);

/* 解析SETUP响应的传输参数 */
int rtsp_parse_setup_response(const char *response, rtsp_session_t *session);

/* 解析DESCRIBE响应（含SDP），并构建后续SETUP使用的URL */
int rtsp_parse_describe_response(rtsp_session_t *session, const char *request_url, const char *response);

// /* 基于session->setup_url自动发起SETUP */
// int rtsp_client_setup_auto(rtsp_session_t *session, const char *transport);

/* 发送ANNOUNCE请求（包含SDP）*/
int rtsp_client_announce(rtsp_session_t *session, const char *url, const char *sdp);

/* 发送RECORD请求 */
int rtsp_client_record(rtsp_session_t *session, const char *url);
#endif
