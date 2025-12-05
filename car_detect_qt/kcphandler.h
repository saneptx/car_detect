#ifndef KCPHANDLER_H
#define KCPHANDLER_H
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QDateTime>
#include "ikcp.h"

class KcpHandler : public QObject
{
    Q_OBJECT
public:
    explicit KcpHandler(QObject *parent = nullptr);
    void initKcp(quint32 conv, QHostAddress remoteAddr, quint16 remotePort, QUdpSocket* udpSocket);
private:
    ikcpcb* _kcp = nullptr;
    QUdpSocket* udpSocket_ = nullptr;
    QHostAddress remoteAddr_;
    quint16 remotePort_ = 0;
    QTimer* updateTimer_ = nullptr;
private slots:
    // 负责定时调用 ikcp_update，驱动 KCP 内部状态机
    void updateKcp();
public:
    // 暴露给外部调用，用于发送数据
    int send(const char* buf, int len);
public slots:
    // **核心：处理 QUdpSocket 接收到的数据**
    void handleReadyRead(QString stringName);

private:
    // KCP 的底层输出函数：KCP 需要发送数据时会调用此函数
    static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);

    // KCP 数据到达时的处理函数：KCP 收到完整可靠数据包时会调用此函数
    void handleKcpRecv(QString stringName);
signals:
    void dataReceived(QString stringName,QByteArray receivedData);
};

#endif // KCPHANDLER_H
