#ifndef MONITORCLIENTWIDGET_H
#define MONITORCLIENTWIDGET_H

#include <QWidget>
#include <QtNetwork/QtNetwork>
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include "h264decoder.h"
#include <QMap>



class MonitorClientWidget: public QWidget{
    Q_OBJECT
public:
     explicit MonitorClientWidget(QWidget *parent = nullptr);
private slots:
    void onConnected();
    void onError(QAbstractSocket::SocketError);
    void onReadyRead();

private:
    void processBuffer();
    void handleFrame(const QString &streamName,
                     const QByteArray &frame, quint32 ts);

    QTcpSocket *_socket;
    QByteArray _buffer;
    QGridLayout *_grid;
    QMap<QString, H264Decoder*> _decoders;
    QMap<QString, QLabel*> _videoWidgets;
};

#endif // MONITORCLIENTWIDGET_H
