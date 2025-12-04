#define _GNU_SOURCE
#include "rtsp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "log.h"

/* 初始化RTSP会话 */
int rtsp_session_init(rtsp_session_t *session) {
    memset(session, 0, sizeof(rtsp_session_t));
    session->state = RTSP_STATE_INIT;
    session->cseq = 0;
    session->rtp_seq = 0;
    session->rtp_ssrc = 0x12345678;
    session->rtp_timestamp = 0;
    session->client.fd = -1;
    session->rtp_socket.fd = -1;
    return 0;
}

/**
 * RTP头格式（12字节）:
 *  0-1bit: 版本号(2)
 *  2bit: Padding
 *  3bit: Extension
 *  4-7bit: CSRC计数
 *  8bit: Marker (关键帧标志)
 *  9-15bit: Payload Type (H.264通常为96)
 *  16-31bit: Sequence Number
 *  32-63bit: Timestamp
 *  64-95bit: SSRC
 */
void build_rtp_header(uint8_t *header, uint16_t seq, uint32_t timestamp,
    uint32_t ssrc, uint8_t payload_type, bool marker)
{
    // printf("RTP: seq=%u ts=%u M=%d\n", (unsigned)seq, (unsigned)timestamp, marker);
    header[0] = 0x80; // Version=2, P=0, X=0, CC=0
    header[1] = payload_type & 0x7F;
    if (marker) header[1] |= 0x80;
    header[2] = seq >> 8;
    header[3] = seq & 0xFF;
    header[4] = (timestamp >> 24) & 0xFF;
    header[5] = (timestamp >> 16) & 0xFF;
    header[6] = (timestamp >> 8) & 0xFF;
    header[7] = timestamp & 0xFF;
    header[8] = (ssrc >> 24) & 0xFF;
    header[9] = (ssrc >> 16) & 0xFF;
    header[10] = (ssrc >> 8) & 0xFF;
    header[11] = ssrc & 0xFF;
}

static inline uint16_t next_seq(rtsp_session_t *sess) {
    return (uint16_t)atomic_fetch_add(&sess->rtp_seq, 1);
}

int send_rtp_over_tcp(rtsp_session_t *sess, const uint8_t *rtp_data, size_t rtp_len, uint8_t channel)
{
    if (!sess || sess->client.fd < 0) {
        fprintf(stderr, "RTP over TCP: invalid session or socket\n");
        return -1;
    }

    uint8_t header[4];
    header[0] = '$';
    header[1] = channel;
    header[2] = (uint8_t)((rtp_len >> 8) & 0xFF);
    header[3] = (uint8_t)(rtp_len & 0xFF);

    int ret = 0;
    size_t total_len = 4 + rtp_len;  // header 长度为 4 字节
    char *buffer = (char *)malloc(total_len);
    if (buffer == NULL) {
        // 处理内存分配失败
        return -1;
    }
    // 复制 header 和 rtp_data 到 buffer 中
    memcpy(buffer, header, 4);           // 将 header 复制到 buffer 开头
    memcpy(buffer + 4, rtp_data, rtp_len);  // 将 rtp_data 复制到 buffer 中 header 后面的位置
    // 一次性发送合并后的数据
    // pthread_mutex_lock(&sess->rtp_send_mtx);
    ret = tcp_write(&sess->client, buffer, total_len);
    // LOG_DEBUG("send %d seq, %d data",(buffer[6]<<8|buffer[7]),ret);
    // pthread_mutex_unlock(&sess->rtp_send_mtx);
    // 发送完成后释放内存
    free(buffer);
    return ret;
}

void rtp_send_h264(rtsp_session_t *session, uint32_t *timestamp,
    const uint8_t *nalu, size_t nalu_size)
{
    if (!session || !nalu || nalu_size == 0) {
        return;
    }
    uint8_t rtp_header[RTP_HEADER_SIZE];
    if(4 + nalu_size + RTP_HEADER_SIZE <= MTU){
        uint8_t packet[MTU-4];//rtsp区分留四个字节空位
        uint8_t nal_type = nalu[0] & 0x1F;
        bool marker = true;
        // if(nal_type == 6){
        //     return;
        // }
        if(nal_type==7||nal_type==8||nal_type == 6){
            marker = false;
        }
        // LOG_DEBUG("nal type:%d,size:%d",nal_header & 0x1F,nalu_size+4+RTP_HEADER_SIZE);
        uint16_t seq = next_seq(session);
        build_rtp_header(rtp_header, seq, *timestamp, session->rtp_ssrc, 96, marker);
        memcpy(packet, rtp_header, RTP_HEADER_SIZE);
        memcpy(packet + RTP_HEADER_SIZE, nalu, nalu_size);
        size_t pkt_len = RTP_HEADER_SIZE + nalu_size;

        if (strcmp(session->transType, "tcp") == 0)
            send_rtp_over_tcp(session, packet, pkt_len, session->rtpChannel);
        else
            // udp_send(&session->rtp_socket, packet, pkt_len);
            send_rtp_over_kcp(packet,pkt_len,session->kcp);

    }else{
        // FU-A 分片发送
        uint8_t packet[MTU-4];//rtsp区分留四个字节空位
        uint8_t nal_header = nalu[0];
        const uint8_t *payload = nalu + 1;           // skip original header
        size_t payload_size = nalu_size - 1;         // real payload

        size_t pos = 0;
        bool isStart = true;

        while (pos < payload_size) {
            //18 = 12(RTP头部) + 4(用于RTSP区分) +2(分片额外负载)
            size_t len = (payload_size - pos > (MTU - 18))
                        ? (MTU - 18)
                        : (payload_size - pos);

            bool isLast = (pos + len >= payload_size);
            /*
            +---+---+---+---------------------+
            | F |   NRI |      Type   (5bit)  |
            +---+---+---+---------------------+
            */
            uint8_t fu_ind = (nal_header & 0xE0) | 28;
            /*
            +---+---+---+---------------------+
            | S | E | R |    NAL Type (5bit)  |
            +---+---+---+---------------------+
            */
            uint8_t fu_hdr = (isStart ? 0x80 : 0x00)
                        | (isLast ? 0x40 : 0x00)
                        | (nal_header & 0x1F);

            size_t offset = 0;
            uint16_t seq = next_seq(session);
            build_rtp_header(rtp_header, seq, *timestamp, session->rtp_ssrc, 96, isLast);
            memcpy(packet + offset, rtp_header, RTP_HEADER_SIZE);
            offset += RTP_HEADER_SIZE;
            packet[offset++] = fu_ind;
            packet[offset++] = fu_hdr;

            memcpy(packet + offset, payload + pos, len);
            offset += len;

            // send rtp...
            if (strcmp(session->transType, "tcp") == 0)
                send_rtp_over_tcp(session, packet, offset, session->rtpChannel);
            else
                // udp_send(&session->rtp_socket, packet, offset);
                send_rtp_over_kcp(packet,offset,session->kcp);
            pos += len;
            isStart = false;
        }
    }
}

/* 清理RTSP会话 */
void rtsp_session_cleanup(rtsp_session_t *session) {
    if (!session) {
        return;
    }
    
    udp_close(&session->rtp_socket);
    tcp_close_client(&session->client);
    session->state = RTSP_STATE_INIT;
}

/* ========== RTSP客户端功能实现 ========== */

/* 发送RTSP OPTIONS请求 */
int rtsp_client_options(rtsp_session_t *session, const char *url) {
    char request[512];
    session->cseq++;
    
    int len = snprintf(request, sizeof(request),
        "OPTIONS %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "User-Agent: RTSP Client\r\n"
        "\r\n",
        url, session->cseq);
    
    printf("发送OPTIONS请求:\n%s\n", request);
    return tcp_write(&session->client, request, len);
}

/* 发送RTSP DESCRIBE请求 */
int rtsp_client_describe(rtsp_session_t *session, const char *url) {
    char request[512];
    session->cseq++;
    
    int len = snprintf(request, sizeof(request),
        "DESCRIBE %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "User-Agent: RTSP Client\r\n"
        "Accept: application/sdp\r\n"
        "\r\n",
        url, session->cseq);
    
    printf("发送DESCRIBE请求:\n%s\n", request);
    return tcp_write(&session->client, request, len);
}

/* 发送RTSP SETUP请求 */
int rtsp_client_setup(rtsp_session_t *session, const char *url, const char *transport) {
    char request[512];
    session->cseq++;
    char setup_url[512];
    snprintf(setup_url, sizeof(setup_url), "%s/trackID=0", url);
    int len = snprintf(request, sizeof(request),
        "SETUP %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "Transport: %s\r\n"
        "User-Agent: RTSP Client\r\n"
        "\r\n",
        setup_url, session->cseq, transport);
    
    printf("发送SETUP请求:\n%s\n", request);
    return tcp_write(&session->client, request, len);
}


/* 发送RTSP TEARDOWN请求 */
int rtsp_client_teardown(rtsp_session_t *session, const char *url) {
    char request[512];
    session->cseq++;
    
    int len = snprintf(request, sizeof(request),
        "TEARDOWN %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "Session: %s\r\n"
        "User-Agent: RTSP Client\r\n"
        "\r\n",
        url, session->cseq, session->session_id);
    
    printf("发送TEARDOWN请求:\n%s\n", request);
    return tcp_write(&session->client, request, len);
}

/* 发送RTSP ANNOUNCE请求（携带SDP） */
int rtsp_client_announce(rtsp_session_t *session, const char *url, const char *sdp) {
    char request[1024];
    int sdp_len = (int)strlen(sdp);
    session->cseq++;
    int len = snprintf(request, sizeof(request),
        "ANNOUNCE %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "User-Agent: RTSP Client\r\n"
        "\r\n"
        "%s",
        url, session->cseq, sdp_len, sdp);
    printf("发送ANNOUNCE请求:\n%s\n", request);
    return tcp_write(&session->client, request, len);
}

/* 发送RTSP RECORD请求（开始推流） */
int rtsp_client_record(rtsp_session_t *session, const char *url) {
    char request[512];
    session->cseq++;
    int len = snprintf(request, sizeof(request),
        "RECORD %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "Session: %s\r\n"
        "User-Agent: RTSP Client\r\n"
        "KcpId: %d\r\n"
        "\r\n",
        url, session->cseq, session->session_id,session->kcp->conv);
    printf("发送RECORD请求:\n%s\n", request);
    return tcp_write(&session->client, request, len);
}

/* 接收RTSP响应 */
int rtsp_client_read_response(rtsp_session_t *session, char *response, int len) {
    int n = tcp_read(&session->client, response, len);
    if (n > 0) {
        printf("收到响应:\n%s\n", response);
    }
    return n;
}

int rtsp_parse_setup_response(const char *response, rtsp_session_t *session)
{
    if (!response || !session) return -1;

    /* 初始化所有关键字段 */
    session->server_rtp_port  = 0;
    session->server_rtcp_port = 0;
    session->rtpChannel       = 0;
    session->rtcpChannel      = 0;
    session->transType[0]     = '\0';
    session->session_id[0]    = '\0';

    const char *p = NULL;

    /* ---- 解析 Session ID ---- */
    // 用 strcasestr 忽略大小写（若不可用，可写自定义版本）
#ifdef _GNU_SOURCE
    p = strcasestr(response, "Session:");
#else
    // 手动忽略大小写匹配
    const char *lower = response;
    while (*lower) {
        if (tolower(*lower) == 's' && strncasecmp(lower, "Session:", 8) == 0) {
            p = lower;
            break;
        }
        lower++;
    }
#endif

    if (p) {
        p += 8;
        while (*p == ' ') p++; // 跳过空格
        int i = 0;
        while (*p && *p != '\r' && *p != ';' && !isspace((unsigned char)*p) && i < 63) {
            session->session_id[i++] = *p++;
        }
        session->session_id[i] = '\0';
    }

    /* ---- 解析 UDP server_port ---- */
    p = strcasestr(response, "server_port=");
    if (p) {
        snprintf(session->transType, sizeof(session->transType), "udp");
        p += strlen("server_port=");
        session->server_rtp_port = (uint16_t)strtol(p, NULL, 10);

        const char *dash = strchr(p, '-');
        if (dash)
            session->server_rtcp_port = (uint16_t)strtol(dash + 1, NULL, 10);

        return 0;
    }

    /* ---- 解析 TCP interleaved ---- */
    p = strcasestr(response, "interleaved=");
    if (p) {
        snprintf(session->transType, sizeof(session->transType), "tcp");
        p += strlen("interleaved=");
        session->rtpChannel = (uint16_t)strtol(p, NULL, 10);

        const char *dash = strchr(p, '-');
        if (dash)
            session->rtcpChannel = (uint16_t)strtol(dash + 1, NULL, 10);

        return 0;
    }

    return -1;
}

/* 解析DESCRIBE响应（提取Content-Base、SDP的control/rtpmap等）并构建setup_url */
static void trim_spaces(char *s) {
    if (!s) return;
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ' || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void build_setup_url(rtsp_session_t *session, const char *request_url) {
    const char *base = session->content_base[0] ? session->content_base : request_url;
    const char *ctrl = session->control_attr[0] ? session->control_attr : "";

    if (starts_with(ctrl, "rtsp://")) {
        snprintf(session->setup_url, RTSP_MAX_URL, "%s", ctrl);
        return;
    }

    /* ctrl 可能是相对的，如 "streamid=0" 或 "trackID=1" */
    size_t blen = strlen(base);
    int need_slash = (blen > 0 && base[blen-1] != '/' && ctrl[0] && ctrl[0] != '/') ? 1 : 0;
    int n;
    if (need_slash)
        n = snprintf(session->setup_url, RTSP_MAX_URL, "%s/%s", base, ctrl);
    else
        n = snprintf(session->setup_url, RTSP_MAX_URL, "%s%s", base, ctrl);

    // 安全检查：若截断则手动补 '\0'
    if (n >= RTSP_MAX_URL) {
        session->setup_url[RTSP_MAX_URL - 1] = '\0';
    #ifdef DEBUG
        fprintf(stderr, "⚠️ setup_url truncated (%d bytes)\n", n);
    #endif
    }
}

int rtsp_parse_describe_response(rtsp_session_t *session, const char *request_url, const char *response) {
    if (!session || !response) return -1;

    session->content_base[0] = '\0';
    session->control_attr[0] = '\0';
    session->setup_url[0] = '\0';
    session->payload_type = 0;
    session->clock_rate = 0;

    /* 头与body分隔 */
    const char *body = strstr(response, "\r\n\r\n");
    if (!body) return -1;
    body += 4;

    /* 解析Content-Base（可选） */
    const char *cb = strstr(response, "Content-Base:");
    if (cb) {
        cb += 13;
        while (*cb == ' ') cb++;
        char line[RTSP_MAX_URL];
        int i = 0;
        while (cb[i] && cb[i] != '\r' && i < (int)sizeof(line)-1) {
            line[i] = cb[i];
            i++;
        }
        line[i] = '\0';
        trim_spaces(line);
        snprintf(session->content_base, sizeof(session->content_base), "%s", line);
    }

    /* 逐行解析SDP */
    const char *p = body;
    while (*p) {
        char line[256];
        int i = 0;
        while (p[i] && p[i] != '\n' && i < (int)sizeof(line)-1) {
            line[i] = p[i];
            i++;
        }
        line[i] = '\0';
        if (p[i] == '\n') p += i + 1; else p += i;
        trim_spaces(line);
        if (line[0] == '\0') continue;

        /* a=control:xxx */
        if (starts_with(line, "a=control:")) {
            const char *ctrl = line + 10;
            trim_spaces((char*)ctrl);
            snprintf(session->control_attr, sizeof(session->control_attr), "%s", ctrl);
        }
        /* a=rtpmap:96 H264/90000 */
        else if (starts_with(line, "a=rtpmap:")) {
            const char *val = line + 9;
            int pt = 0;
            char enc[32] = {0};
            int clock = 0;
            /* 简单解析: rtpmap:<pt> <enc>/<clock> */
            if (sscanf(val, "%d %31[^/]/%d", &pt, enc, &clock) == 3) {
                session->payload_type = pt;
                session->clock_rate = clock;
            }
        }
    }

    build_setup_url(session, request_url);
    return 0;
}

// int rtsp_client_setup_auto(rtsp_session_t *session, const char *transport) {
//     if (!session || session->setup_url[0] == '\0') return -1;
//     return rtsp_client_setup(session, session->setup_url, transport);
// }
