#ifndef _TCP_H_
#define _TCP_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* TCP客户端连接结构 */
typedef struct tcp_client {
    int fd;                      /* socket文件描述符 */
    struct sockaddr_in addr;     /* 服务器地址信息 */
} tcp_client_t;

/* TCP客户端连接到服务器
 * 参数:
 *   client: TCP客户端结构指针
 *   server_ip: 服务器IP地址（字符串格式，如"192.168.1.100"）
 *   port: 服务器端口号
 * 返回:
 *   成功返回0，失败返回-1
 */
int tcp_client_connect(tcp_client_t *client, const char *server_ip, int port);

/* 从服务器接收数据
 * 参数:
 *   client: TCP客户端结构指针
 *   buf: 接收缓冲区
 *   len: 缓冲区大小
 * 返回:
 *   成功返回接收的字节数，失败返回-1
 *   注意: buf会自动添加字符串结束符'\0'
 */
int tcp_read(tcp_client_t *client, char *buf, int len);

/* 向服务器发送数据
 * 参数:
 *   client: TCP客户端结构指针
 *   buf: 要发送的数据缓冲区
 *   len: 要发送的数据长度
 * 返回:
 *   成功返回发送的字节数，失败返回-1
 */
int tcp_write(tcp_client_t *client, const char *buf, int len);

/* 关闭TCP客户端连接
 * 参数:
 *   client: TCP客户端结构指针
 */
void tcp_close_client(tcp_client_t *client);

#endif
