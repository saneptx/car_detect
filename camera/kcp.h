#ifndef _KCP_H_
#define _KCP_H_

#include "ikcp.h"
#include "udp.h"

#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#define KCP_MTU 1450
#define KCP_INTERVAL 10
#define KCP_WNDSIZE 128

// static ikcpcb *kcp = NULL;

// KCP 输出回调：KCP 编码后的数据通过 UDP 发送
ikcpcb* kcp_init(udp_socket_t *udpSocket);
void send_rtp_over_kcp(const uint8_t *rtp_data, int rtp_len,ikcpcb *kcp);
uint32_t iclock();
uint32_t rand_u32();
// 定时器线程（单独创建，避免阻塞主逻辑）
void* kcp_timer_thread(void *arg);
#endif