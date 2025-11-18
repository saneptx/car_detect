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
                     const QByteArray &frame, quint32 ts, quint64 sendTimeUs);

    struct StreamStats {
        quint64 frameCount = 0;
        quint64 networkDelayAccumUs = 0;
        quint64 decodeDelayAccumUs = 0;
    };

<<<<<<< ours
<<<<<<< ours
    QTcpSocket *_socket;
    QByteArray _buffer;
    QGridLayout *_grid;
    QMap<QString, H264Decoder*> _decoders;
    QMap<QString, QLabel*> _videoWidgets;
    QMap<QString, int> _pendingDecodes;
    QMap<QString, QSharedPointer<QMutex>> _decoderLocks;
    QMap<QString, StreamStats> _stats;

    static constexpr int kMaxPendingPerStream = 3;
    static constexpr int kLogIntervalFrames = 60;
};

#endif // MONITORCLIENTWIDGET_H
=======
=======
>>>>>>> theirs
    QTcpSocket *_socket;
    QByteArray _buffer;
    QGridLayout *_grid;
    QMap<QString, H264Decoder*> _decoders;
    QMap<QString, QLabel*> _videoWidgets;
};

#endif // MONITORCLIENTWIDGET_H
<<<<<<< ours
>>>>>>> theirs
=======
>>>>>>> theirs
