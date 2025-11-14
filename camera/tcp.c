#include "tcp.h"

/* TCP客户端连接到服务器 */
int tcp_client_connect(tcp_client_t *client, const char *server_ip, int port) {
    /* 创建TCP socket */
    client->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->fd < 0) {
        perror("socket");
        return -1;
    }
    
    /* 初始化服务器地址结构 */
    memset(&client->addr, 0, sizeof(client->addr));
    client->addr.sin_family = AF_INET;
    client->addr.sin_port = htons(port);
    
    /* 将IP地址字符串转换为网络字节序 */
    if (inet_aton(server_ip, &client->addr.sin_addr) == 0) {
        fprintf(stderr, "错误: 无效的IP地址: %s\n", server_ip);
        close(client->fd);
        client->fd = -1;
        return -1;
    }
    
    /* 连接到服务器 */
    if (connect(client->fd, (struct sockaddr *)&client->addr, sizeof(client->addr)) < 0) {
        perror("connect");
        close(client->fd);
        client->fd = -1;
        return -1;
    }
    
    printf("已连接到服务器: %s:%d\n", server_ip, port);
    return 0;
}

/* 从服务器接收数据 */
int tcp_read(tcp_client_t *client, char *buf, int len) {
    int n;
    
    if (client->fd < 0) {
        fprintf(stderr, "错误: TCP连接未建立\n");
        return -1;
    }
    
    /* 接收数据（留一个字节用于字符串结束符） */
    n = recv(client->fd, buf, len - 1, 0);
    if (n < 0) {
        perror("recv");
        return -1;
    }
    
    if (n == 0) {
        /* 服务器关闭了连接 */
        printf("服务器关闭了连接\n");
        return 0;
    }
    
    /* 添加字符串结束符 */
    buf[n] = '\0';
    return n;
}

/* 向服务器发送数据 */
// int tcp_write(tcp_client_t *client, const char *buf, int len) {
//     int sent = 0;
//     int n;
    
//     if (client->fd < 0) {
//         fprintf(stderr, "错误: TCP连接未建立\n");
//         return -1;
//     }
    
//     /* 确保所有数据都被发送（循环发送直到全部发送完成） */
//     while (sent < len) {
//         n = send(client->fd, buf + sent, len - sent, 0);
//         if (n < 0) {
//             perror("send");
//             return -1;
//         }
//         sent += n;
//     }
//     printf("Tcp send %d data\n",sent);
//     return sent;
// }

int tcp_write(tcp_client_t *client,const char *buf,int len){
    int left = len;
    const char *pstr = buf;
    int ret = 0;
    while(left > 0){
        ret = write(client->fd, pstr, left);
        if(ret == -1){
            if(errno == EINTR){
                continue;
            } else if(errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞写，暂时写不了
                return len - left; // 或者返回 0
            } else {
                printf("Writen ERROR");
                return -1;
            }
        } else if(ret == 0){
            break; // 理论上 write 不会返回 0，除非 fd 非常异常
        } else {
            pstr += ret;
            left -= ret;
        }
    }
    return len - left;//返回实际写入的字节数
}

/* 关闭TCP客户端连接 */
void tcp_close_client(tcp_client_t *client) {
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
        printf("TCP连接已关闭\n");
    }
}

