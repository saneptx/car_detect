#include "udp.h"

/* 初始化UDP socket */
int udp_init(udp_socket_t *udp, const char *ip, int port) {
    udp->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp->fd < 0) {
        perror("udp socket");
        return -1;
    }
    
    memset(&udp->addr, 0, sizeof(udp->addr));
    udp->addr.sin_family = AF_INET;
    udp->addr.sin_port = htons(port);
    
    if (ip) {
        if (inet_aton(ip, &udp->addr.sin_addr) == 0) {
            fprintf(stderr, "无效的IP地址: %s\n", ip);
            close(udp->fd);
            return -1;
        }
    } else {
        udp->addr.sin_addr.s_addr = INADDR_ANY;
    }
    
    printf("UDP socket初始化: %s:%d\n", ip ? ip : "0.0.0.0", port);
    return 0;
}

/* 发送数据 */
int udp_send(udp_socket_t *udp, const void *buf, int len) {
    int n = sendto(udp->fd, buf, len, 0, 
                   (struct sockaddr *)&udp->addr, sizeof(udp->addr));
    // LOG_DEBUG("UDP SEND %d data",n);
    if (n < 0) {
        perror("udp sendto");
        return -1;
    }
    return n;
}

int udp_recv(udp_socket_t *udp, void *buf, int len){
    int n = recv(udp->fd, buf, len,MSG_DONTWAIT);
    return n;
}

/* 关闭UDP socket */
void udp_close(udp_socket_t *udp) {
    if (udp->fd >= 0) {
        close(udp->fd);
        udp->fd = -1;
    }
}

