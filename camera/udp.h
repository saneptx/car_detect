#ifndef _UDP_H_
#define _UDP_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "log.h"

/* UDP socket结构 */
typedef struct udp_socket {
    int fd;
    struct sockaddr_in addr;
} udp_socket_t;

/* 初始化UDP socket */
int udp_init(udp_socket_t *udp, const char *ip, int port);

/* 发送数据 */
int udp_send(udp_socket_t *udp, const void *buf, int len);

int udp_recv(udp_socket_t *udp, void *buf, int len);
/* 关闭UDP socket */
void udp_close(udp_socket_t *udp);

#endif

