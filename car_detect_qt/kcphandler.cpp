#include "kcphandler.h"

KcpHandler::KcpHandler(QObject *parent)
    :QObject(parent)
{

}

void KcpHandler::initKcp(quint32 conv, QHostAddress remoteAddr, quint16 remotePort, QUdpSocket* udpSocket) {
    // ... KCP 初始化代码 ...
    remoteAddr_ = remoteAddr;
    remotePort_ = remotePort;
    udpSocket_ = udpSocket;
    _kcp = ikcp_create(conv, this);
    _kcp->output = udp_output;
    ikcp_nodelay(_kcp, 1, 10, 2, 0);
    ikcp_wndsize(_kcp, 128, 128);
    ikcp_setmtu(_kcp, 1450);
    // 设置 KCP 定时器，例如每 10 毫秒或更短
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, &KcpHandler::updateKcp);
    updateTimer_->start(10); // 10ms
}

int KcpHandler::udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    // user 参数指向 KcpHandler 实例
    KcpHandler* handler = static_cast<KcpHandler*>(user);

    if (handler && handler->udpSocket_) {
        // 使用 QUdpSocket 将数据报发送到远端地址
        return handler->udpSocket_->writeDatagram(buf, len, handler->remoteAddr_, handler->remotePort_);
    }
    return -1;
}

void KcpHandler::handleReadyRead(QString stringName)
{
    if (!udpSocket_ || !_kcp) return;

    while(udpSocket_->hasPendingDatagrams()){
        qint64 datagramSize = udpSocket_->pendingDatagramSize();
        QByteArray datagram;
        datagram.resize(static_cast<int>(datagramSize));

        QHostAddress senderAddress;
        quint16 senderPort;

        udpSocket_->readDatagram(datagram.data(), datagram.size(),
                                          &senderAddress, &senderPort);
        // **核心：将原始 UDP 数据报喂给 KCP**
        ikcp_input(_kcp, datagram.data(), datagram.size());

        // 检查 KCP 是否有完整数据可以取出
        handleKcpRecv(stringName);

    }
}

void KcpHandler::updateKcp()
{
    if (_kcp) {
        // ikcp_update 的参数应为当前毫秒时间
        ikcp_update(_kcp, QDateTime::currentMSecsSinceEpoch());
    }
}

void KcpHandler::handleKcpRecv(QString stringName)
{
    if (!_kcp) return;

    // KCP 缓冲区最大尺寸，根据实际情况调整
    char recv_buf[4096];
    int hr;

    // 循环调用 ikcp_recv 直到没有完整数据包可读
    while ((hr = ikcp_recv(_kcp, recv_buf, sizeof(recv_buf))) > 0) {
        // **成功接收到可靠数据！**
        // hr 是接收到的数据长度
        QByteArray receivedData(recv_buf, hr);

        // **在这里处理您的业务逻辑**，例如：
        // _h264RtpReassmbler.handleRtp(stringName, receivedData);
//        qDebug() << "KCP received reliable data of size:" << hr;
        // 触发一个信号通知上层应用
        emit dataReceived(stringName,receivedData);
    }
}
