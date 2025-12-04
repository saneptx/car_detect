#include "kcp.h"

static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    return udp_send((udp_socket_t *)user,buf,len);
}
ikcpcb* kcp_init(udp_socket_t *udpSocket){
    uint32_t conv = rand_u32();
    ikcpcb* kcp = ikcp_create(conv, udpSocket);
    kcp->output = kcp_output;
    ikcp_nodelay(kcp, 1, KCP_INTERVAL, 2, 0);
    ikcp_wndsize(kcp, KCP_WNDSIZE, KCP_WNDSIZE);
    ikcp_setmtu(kcp, KCP_MTU);
    return kcp;
}
void send_rtp_over_kcp(const uint8_t *rtp_data, int rtp_len,ikcpcb *kcp) {
    if (!kcp || rtp_len <= 0) return;
    // KCP 发送 RTP 数据（自动拆分/编码）
    ikcp_send(kcp, (const char*)rtp_data, rtp_len);
}

void* kcp_timer_thread(void *arg) {
    ikcpcb *kcp = (ikcpcb*)arg;
    while (1) {
        // 调用 ikcp_update()，传入当前毫秒数
        ikcp_update(kcp, iclock());
        // 休眠 10ms（和 KCP interval 一致）
        usleep(10 * 1000); // 10ms = 10000 微秒
    }
    return NULL;
}

inline IUINT32 iclock() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    // 简化：使用秒*1000 + 毫秒
    return (IUINT32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

uint32_t rand_u32() {
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}