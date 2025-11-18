#ifndef MONITORCLIENTWIDGET_H
#define MONITORCLIENTWIDGET_H

#include <QWidget>
#include <QtNetwork/QtNetwork>
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QMutex>
#include <QSharedPointer>
#include "h264decoder.h"



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
    QMap<QString, int> _pendingDecodes;
    QMap<QString, QSharedPointer<QMutex>> _decoderLocks;

    static constexpr int kMaxPendingPerStream = 3;
};

#endif // MONITORCLIENTWIDGET_H
