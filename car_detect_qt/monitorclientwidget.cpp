#include "monitorclientwidget.h"

#include <QVBoxLayout>
#include <QDebug>
#include <QtEndian>
#include <QNetworkProxy>
#include <QMap>
#include <QUdpSocket>
#include <QTcpServer>
#include <QHostAddress>
#include <QDebug>

static const char *SERVER_IP   = "192.168.5.11";
static const quint16 SERVER_PORT = 9000;

MonitorClientWidget::MonitorClientWidget(QWidget *parent)
    : QWidget(parent),
      _socket(new QTcpSocket(this)),
      _grid(new QGridLayout),
      _decoderMgr(this)   // 让 MultiStreamDecoder 以本 widget 为 parent
{
    qDebug() << "MonitorClientWidget()";
    auto layout = new QVBoxLayout(this);
    layout->addLayout(_grid);
    f = fopen("debug.h264", "wb");
    connect(_socket, &QTcpSocket::connected,
            this, &MonitorClientWidget::onConnected);
    connect(_socket, &QTcpSocket::readyRead,
            this, &MonitorClientWidget::onReadyRead);
    connect(_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &MonitorClientWidget::onError);

    _socket->setProxy(QNetworkProxy::NoProxy);
    _socket->connectToHost(QString::fromLatin1(SERVER_IP), SERVER_PORT);
    // H264RtpReassembler 组好一"帧" H.264 时回调这里
    _h264RtpReassmbler.onFrameReady = [this](const QString &streamName, const QByteArray &frame) {
        // frame 是完整一帧 H.264（含 00 00 00 01 start code）
        handleFrame(streamName, frame);
    };
}

MonitorClientWidget::~MonitorClientWidget(){
    fclose(f);
}

void MonitorClientWidget::onConnected()
{
    qDebug() << "Connected to monitor server";
    sendSetup();
}

void MonitorClientWidget::onError(QAbstractSocket::SocketError err)
{
    qWarning() << "Socket error:" << err << _socket->errorString();
}

void MonitorClientWidget::onReadyRead()
{
    _buffer.append(_socket->readAll());
    int requestEnd = _buffer.indexOf("\r\n\r\n");
    QString request = _buffer.mid(0,requestEnd);
    qDebug()<<"Handing Request"<<request;
    _buffer.remove(0,requestEnd+5);
    QString first;
    QString second;
    QMap<QString,QString> headers;
    parseRespond(request,first,second,headers);
    if(headers.contains("CamNum")&&headers["CamNum"]!="0"){
        for(int i=0;i<headers["CamNum"].toInt();i++){
            QString stringName = headers[QString::number(i)];
            transUdpPort udpPort;
            udpPort._udpRtpSocket = new QUdpSocket(this);
            udpPort._udpRtpSocket->bind(_socket->localAddress(), 0);

            KcpHandler* kcphandler = new KcpHandler(this);
            _mtx.lock();
            ++_conv;
            _mtx.unlock();
            udpPort.conv = _conv;
            QHostAddress addr = _socket->peerAddress();
            kcphandler->initKcp(_conv,addr,8910,udpPort._udpRtpSocket);
            connect(udpPort._udpRtpSocket, &QUdpSocket::readyRead, [kcphandler,stringName]{
                kcphandler->handleReadyRead(stringName);
            });
            connect(kcphandler,&KcpHandler::dataReceived,
                             &_h264RtpReassmbler,
                             &H264RtpReassembler::handleRtp);
            udpPort._udpRtcpSocket = new QUdpSocket(this);
            udpPort._udpRtcpSocket->bind(_socket->localAddress(), 0);
            _camMap[stringName] = udpPort;
        }
        sendMessage();
    }else if(first == "ADDCAM"){
        if(headers.contains("SessionId")){
            QString stringName = headers["SessionId"];
            transUdpPort udpPort;
            udpPort._udpRtpSocket = new QUdpSocket(this);
            udpPort._udpRtpSocket->bind(_socket->localAddress(), 0);

            KcpHandler* kcphandler = new KcpHandler(this);
            _mtx.lock();
            ++_conv;
            _mtx.unlock();
            udpPort.conv = _conv;
            QHostAddress addr = _socket->peerAddress();
            kcphandler->initKcp(_conv,addr,8910,udpPort._udpRtpSocket);
            connect(udpPort._udpRtpSocket, &QUdpSocket::readyRead, kcphandler, [kcphandler,stringName]{
                kcphandler->handleReadyRead(stringName);
            });
            connect(kcphandler,&KcpHandler::dataReceived, // <-- 使用 &KcpHandler::信号名
                             &_h264RtpReassmbler,      // <-- 接收者对象
                             &H264RtpReassembler::handleRtp); // <-- 使用 &类名::槽名
            udpPort._udpRtcpSocket = new QUdpSocket(this);
            udpPort._udpRtcpSocket->bind(_socket->localAddress(), 0);
            _camMap[stringName] = udpPort;
            sendAddCamRespond(stringName);
        }
    }else if(first == "DELCAM"){
        if(headers.contains("SessionId")){
            QString sessionId = headers["SessionId"];
            _camMap.remove(sessionId);
        }
    }
}
/*
    respond:
        200 OK\r\n
        Cseq: value\r\n
        \r\n
*/
void MonitorClientWidget::parseRespond(const QString& request,
                                       QString& first,
                                       QString& second,
                                       QMap<QString, QString>& headers)
{

    first.clear();
    second.clear();
    headers.clear();

    QString normalizedText = request;
    normalizedText.replace(QString("\r\n"),QChar('\n'));
    QStringList lines_precise = normalizedText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines_precise.isEmpty()) {
        qWarning() << "Request is empty after splitting.";
        return;
    }
    QString startLine = lines_precise.first().trimmed();
    QStringList parts = startLine.split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        // 第一个元素是状态码 (e.g., "200")
        first = parts.value(0);
        // 第二个元素是状态 (e.g., "OK")
        second = parts.value(1);
    } else {
        qWarning() << "Invalid request start line format:" << startLine;
        return;
    }
    for (int i = 1; i < lines_precise.size(); ++i) {
        QString line = lines_precise.at(i).trimmed();

        // 查找第一个 ':' 分隔符
        int separatorIndex = line.indexOf(':');

        if (separatorIndex > 0) {
            // Key 是 ':' 前面的部分
            QString key = line.left(separatorIndex).trimmed();
            // Value 是 ':' 后面的部分
            QString value = line.mid(separatorIndex + 1).trimmed();

            // 存入 QMap
            headers.insert(key, value);
        } else {
            // 忽略格式错误的行（非 Key: Value 格式）
            qWarning() << "Skipping malformed header line:" << line;
        }
    }
}

/*
request:
    METHOD URL\r\n
    Cseq: value\r\n
    Header2: value\r\n
    \r\n
*/
void MonitorClientWidget::sendSetup(){
    QString localIp = _socket->localAddress().toString();
    QString localPortStr = QString::number(_socket->localPort());
    QString request = "SETUP " + localIp + ":" + localPortStr + "\r\n"
                      "Cseq: " + QString::number(++_cseq) + "\r\n"
                      "\r\n";
    QByteArray requestData = request.toUtf8();
    qDebug()<<requestData;
    qint64 bytesWritten = _socket->write(requestData);
    if (bytesWritten == -1) {
        qDebug() << "Error writing to socket:" << _socket->errorString();
    } else if (bytesWritten != requestData.size()) {
        qDebug() << "Warning: Only wrote" << bytesWritten << "of" << requestData.size() << "bytes.";
    }
}

void MonitorClientWidget::sendMessage(){
    QString localIp = _socket->localAddress().toString();
    QString localPortStr = QString::number(_socket->localPort());
    QString request = "MESSAGE " + localIp + ":" + localPortStr + "\r\n"
                      "Cseq: " + QString::number(++_cseq) + "\r\n";
    QMap<QString,transUdpPort>::iterator i;
    for(i = _camMap.begin(); i!=_camMap.end(); ++i){
        request.append(i.key()+": "
                       + QString::number(i.value()._udpRtpSocket->localPort())
                       + " "
                       + QString::number(i.value()._udpRtcpSocket->localPort())
                       + " "
                       + QString::number(i.value().conv)
                       + "\r\n");
    }
    request.append("\r\n");
    QByteArray requestData = request.toUtf8();
    qDebug()<<requestData;
    qint64 bytesWritten = _socket->write(requestData);
    if (bytesWritten == -1) {
        qDebug() << "Error writing to socket:" << _socket->errorString();
    } else if (bytesWritten != requestData.size()) {
        qDebug() << "Warning: Only wrote" << bytesWritten << "of" << requestData.size() << "bytes.";
    }
}

void MonitorClientWidget::sendAddCamRespond(QString sessionId){
    QString localIp = _socket->localAddress().toString();
    QString localPortStr = QString::number(_socket->localPort());
    QString request = "ADDCAM " + localIp + ":" + localPortStr + "\r\n"
                      "Cseq: " + QString::number(++_cseq) + "\r\n";
    request.append(sessionId + ": " + QString::number(_camMap[sessionId]._udpRtpSocket->localPort())
                             + " "
                             + QString::number(_camMap[sessionId]._udpRtcpSocket->localPort())
                             + " "
                             + QString::number(_conv)
                             + "\r\n");
    request.append("\r\n");
    QByteArray requestData = request.toUtf8();
    qDebug()<<requestData;
    qint64 bytesWritten = _socket->write(requestData);
    qDebug()<<"Send ADDCAM Respond to Server";
    if (bytesWritten == -1) {
        qDebug() << "Error writing to socket:" << _socket->errorString();
    } else if (bytesWritten != requestData.size()) {
        qDebug() << "Warning: Only wrote" << bytesWritten << "of" << requestData.size() << "bytes.";
    }
}

// 解析自定义协议
//void MonitorClientWidget::processBuffer()
//{
//    while(true){
//        //先查看有没有nameLen
//        if (_buffer.size() < 2)
//            return;
//        quint16 nameLen = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(_buffer.constData()));//占10个字节
//        int offset = 2;
//        QString streamName = QString::fromUtf8(_buffer.constData() + offset, nameLen);
//        offset += nameLen;//12
//        quint16 pktLen = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(_buffer.constData()) + offset + 2);
//        if(_buffer.size() < offset + 4 + pktLen){
//            //数据没收全
//            return;
//        }
//        QByteArray rtpPacket = _buffer.mid(offset + 4, pktLen);
//        uint16_t seq = ((uint8_t)rtpPacket[2] << 8) | (uint8_t)rtpPacket[3];
//        qDebug()<< streamName<<" "<<seq<<" "<<pktLen;
//        _buffer.remove(0, offset + 4 + pktLen);
//        handleRtpPacket(streamName,rtpPacket);
//    }
//}
// 收到一整帧 H.264（Annex B，含起始码）
void MonitorClientWidget::handleFrame(const QString &streamName, const QByteArray &frame)
{
    if (f) {
        fwrite(frame.data(), 1, frame.size(), f);
    }
    // 如果该 stream 还没有 QLabel + 解码线程，就在这里创建
    if (!_videoWidgets.contains(streamName)) {
        int idx = _videoWidgets.size();
        int row = idx / 2;
        int col = idx % 2;
        auto videoWidget = new VideoOpenGLWidget(this); // 替换 QLabel
        videoWidget->setFixedSize(1920, 1080);
        videoWidget->setObjectName(streamName);
        _grid->addWidget(videoWidget, row, col);
        // 传递新的控件类型
        _decoderMgr.addStream(streamName, videoWidget); // 传递新的控件
        _videoWidgets.insert(streamName, videoWidget); // 更新 Map

        qDebug() << "Created label and decoder pipeline for stream" << streamName;
    }

    // 把这一帧 H264 扔给对应 stream 的解码线程
    _decoderMgr.pushFrame(streamName, frame);
}
