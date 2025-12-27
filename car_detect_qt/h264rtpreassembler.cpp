#include "h264rtpreassembler.h"
#include <QDebug>
#include <algorithm>

H264RtpReassembler::H264RtpReassembler(QObject *parent)
    :QObject(parent)
{
    // 构造函数：可初始化默认参数
}

void H264RtpReassembler::handleRtp(const QString &streamName, const QByteArray &packet)
{
    // 1. 空包校验
    if (packet.isEmpty()) {
//        qWarning() << "[H264RtpReassembler] Empty packet received, stream:" << streamName;
        return;
    }

    // 2. 解析RTP包（带校验）
    RtpPacket pkt = parseRtp(packet);
    if (pkt.payload == nullptr || pkt.payloadLen <= 0) {
//        qWarning() << "[H264RtpReassembler] Invalid RTP payload, stream:" << streamName
//                   << "packet size:" << packet.size();
        return;
    }

    quint16 currentSeq = pkt.seq;

    // 3. 重复包检测（处理序列号回绕）
    bool isDuplicate = false;
    if (_lastSeq != 0) {
        // 正常情况：当前seq <= 上一个seq → 重复
        if (currentSeq <= _lastSeq) {
            // 处理回绕（65535 → 0）：如果上一个seq接近最大值，且当前seq很小，不认为是重复
            if (!(_lastSeq > 65535 - _maxSeqGap && currentSeq < _maxSeqGap)) {
                isDuplicate = true;
            }
        }
    }
    if (isDuplicate) {
//        qDebug() << "[H264RtpReassembler] Duplicate packet, stream:" << streamName
//                 << "seq:" << currentSeq << "lastSeq:" << _lastSeq;
        return;
    }

    // 4. 过大序列号间隔检测（重置状态）
    if (_lastSeq != 0) {
        int seqGap = currentSeq - _lastSeq;
        // 处理回绕场景下的间隔计算
        if (_lastSeq > currentSeq) {
            seqGap = (65535 - _lastSeq) + currentSeq + 1;
        }
        if (seqGap > _maxSeqGap) {
//            qWarning() << "[H264RtpReassembler] Large sequence gap detected, stream:" << streamName
//                       << "lastSeq:" << _lastSeq << "currentSeq:" << currentSeq
//                       << "gap:" << seqGap;
            // 重置所有组包状态，避免脏数据
            _frame.clear();
            _fuBuffer.clear();
            _fuStarted = false;
            _fuExpectedSeq = 0;
            _fuStartSeq = 0;
        }
    }

    // 5. 处理NALU组包
    _processRtpNalu(streamName, pkt);

    // 6. 更新最后处理的序列号
    _lastSeq = currentSeq;
}

void H264RtpReassembler::_processRtpNalu(const QString &streamName, const RtpPacket &pkt)
{
    const uint8_t* payload = pkt.payload;
    int payloadLen = pkt.payloadLen;
    quint16 currentSeq = pkt.seq;

    // 1. 基础校验
    if (payload == nullptr || payloadLen < 1) {
//        qWarning() << "[H264RtpReassembler] Invalid payload, stream:" << streamName
//                   << "seq:" << currentSeq;
        return;
    }

    uint8_t nalType = payload[0] & 0x1F; // NALU类型（5bit）

    // ------------------ 单NALU处理（0 < nalType < 24） ------------------
    if (nalType > 0 && nalType < 24) {
        // 清理残留的FU-A状态（避免交叉组包）
        if (_fuStarted) {
//            qWarning() << "[H264RtpReassembler] Single NALU received while FU-A assembling, reset FU state"
//                       << "stream:" << streamName << "seq:" << currentSeq;
            _fuBuffer.clear();
            _fuStarted = false;
            _fuExpectedSeq = 0;
            _fuStartSeq = 0;
        }

        // 构建完整NALU（带起始码）
        QByteArray naluData;
        appendStartCode4(naluData); // H264通常用4字节起始码
        naluData.append(reinterpret_cast<const char*>(payload), payloadLen);

        // 检查并保存SPS/PPS
        _checkAndSaveSpsPps(nalType, naluData);

        // 添加到当前帧
        _frame.append(naluData);

        // 检查M标记位，输出完整帧
        if (pkt.marker && !_frame.isEmpty()) {
            qDebug() << "[H264RtpReassembler] Complete frame (single NALU), stream:" << streamName
                     << "size:" << _frame.size() << "seq:" << currentSeq;
            emit onFrameReady(streamName, _frame);
            _frame.clear();
        }
    }

    // ------------------ FU-A处理（nalType = 28） ------------------
    else if (nalType == 28) {
        // FU-A必须至少包含FU Indicator + FU Header（2字节）
        if (payloadLen < 2) {
//            qWarning() << "[H264RtpReassembler] Invalid FU-A payload length, stream:" << streamName
//                       << "seq:" << currentSeq << "len:" << payloadLen;
            _fuBuffer.clear();
            _fuStarted = false;
            _fuExpectedSeq = 0;
            _fuStartSeq = 0;
            return;
        }

        uint8_t fuIndicator = payload[0];
        uint8_t fuHeader = payload[1];
        bool isFuStart = (fuHeader & 0x80) != 0; // 起始位（S）
        bool isFuEnd = (fuHeader & 0x40) != 0;   // 结束位（E）
        uint8_t realNalType = fuHeader & 0x1F;   // 真实NALU类型

        // 校验真实NALU类型有效性
        if (realNalType == 0 || realNalType >= 24) {
//            qWarning() << "[H264RtpReassembler] Invalid NAL type in FU-A, stream:" << streamName
//                       << "seq:" << currentSeq << "realType:" << static_cast<int>(realNalType);
            _fuBuffer.clear();
            _fuStarted = false;
            _fuExpectedSeq = 0;
            _fuStartSeq = 0;
            return;
        }

        // ------------------ FU-A起始包 ------------------
        if (isFuStart) {
            // 清理之前未完成的FU-A缓存
            if (_fuStarted) {
//                qWarning() << "[H264RtpReassembler] New FU-A start while previous not completed, discard previous"
//                           << "stream:" << streamName << "newSeq:" << currentSeq
//                           << "oldStartSeq:" << _fuStartSeq;
                _fuBuffer.clear();
            }

            // 初始化FU-A状态
            _fuStarted = true;
            _fuStartSeq = currentSeq;
            _fuExpectedSeq = currentSeq + 1;
            _fuBuffer.clear();

            // 构建NALU头（FU Indicator的前3bit + 真实NALU类型）
            QByteArray unitData;
            uint8_t nalHeader = (fuIndicator & 0xE0) | realNalType;
            unitData.push_back(nalHeader);

            // 添加FU负载（跳过前2字节）
            if (payloadLen > 2) {
                unitData.append(reinterpret_cast<const char*>(payload + 2), payloadLen - 2);
            } else {
//                qWarning() << "[H264RtpReassembler] FU-A start packet has no data, stream:" << streamName
//                           << "seq:" << currentSeq;
                _fuStarted = false;
                _fuExpectedSeq = 0;
                _fuStartSeq = 0;
                return;
            }

            // 加入缓存
            _fuBuffer.insert(currentSeq, {currentSeq, unitData});

            // 检查缓存大小（防溢出）
            if (_fuBuffer.size() > _maxFuBufferSize) {
//                qWarning() << "[H264RtpReassembler] FU buffer overflow, stream:" << streamName;
                _fuBuffer.clear();
                _fuStarted = false;
                _fuExpectedSeq = 0;
                _fuStartSeq = 0;
            }
        }

        // ------------------ FU-A中间/结束包 ------------------
        else if (_fuStarted) {
            // 检查序列号是否在允许范围内
            int seqGap = currentSeq - _fuExpectedSeq;
            if (seqGap < 0) {
                // 乱序包（已缓存过或超出回绕范围）
//                qWarning() << "[H264RtpReassembler] FU-A out-of-order (too old), stream:" << streamName
//                           << "expected:" << _fuExpectedSeq << "received:" << currentSeq;
                return;
            }
            if (seqGap > _maxSeqGap) {
                // 超出最大间隔，丢弃当前FU-A
//                qWarning() << "[H264RtpReassembler] FU-A sequence gap too large, stream:" << streamName
//                           << "expected:" << _fuExpectedSeq << "received:" << currentSeq << "gap:" << seqGap;
                _fuBuffer.clear();
                _fuStarted = false;
                _fuExpectedSeq = 0;
                _fuStartSeq = 0;
                return;
            }

            // 添加当前FU单元到缓存
            QByteArray unitData;
            unitData.append(reinterpret_cast<const char*>(payload + 2), payloadLen - 2);
            _fuBuffer.insert(currentSeq, {currentSeq, unitData});

            // 更新期望的下一个序列号
            if (currentSeq >= _fuExpectedSeq) {
                _fuExpectedSeq = currentSeq + 1;
            }

            // 检查缓存大小
            if (_fuBuffer.size() > _maxFuBufferSize) {
//                qWarning() << "[H264RtpReassembler] FU buffer overflow, stream:" << streamName;
                _fuBuffer.clear();
                _fuStarted = false;
                _fuExpectedSeq = 0;
                _fuStartSeq = 0;
                return;
            }

            // ------------------ FU-A结束包 ------------------
            if (isFuEnd) {
                // 快速校验所有包是否连续（利用QMap有序特性）
                bool isComplete = true;
                quint16 expectedSeq = _fuStartSeq;
                for (auto it = _fuBuffer.begin(); it != _fuBuffer.end(); ++it) {
                    if (it.key() != expectedSeq) {
                        isComplete = false;
                        break;
                    }
                    expectedSeq++;
                }

                // 校验结束条件（最后一个包的seq应等于期望seq-1）
                if (isComplete && (currentSeq == expectedSeq - 1)) {
                    // 拼接完整NALU（带起始码）
                    QByteArray naluData;
                    appendStartCode4(naluData);
                    for (auto& kv : _fuBuffer) {
                        naluData.append(kv.data);
                    }

                    // 检查并保存SPS/PPS
                    _checkAndSaveSpsPps(realNalType, naluData);

                    // 添加到当前帧
                    _frame.append(naluData);

//                    qDebug() << "[H264RtpReassembler] FU-A assembled successfully, stream:" << streamName
//                             << "units:" << _fuBuffer.size() << "startSeq:" << _fuStartSeq
//                             << "endSeq:" << currentSeq << "nalType:" << static_cast<int>(realNalType);
                } else {
                    qWarning() << "[H264RtpReassembler] Incomplete FU-A, stream:" << streamName
                               << "units:" << _fuBuffer.size() << "startSeq:" << _fuStartSeq
                               << "endSeq:" << currentSeq;
                }

                // 重置FU-A状态
                _fuBuffer.clear();
                _fuStarted = false;
                _fuExpectedSeq = 0;
                _fuStartSeq = 0;
            }
        }

        // ------------------ 未开始FU-A时收到中间包 ------------------
        else {
            qWarning() << "[H264RtpReassembler] FU-A packet received but not started, stream:" << streamName
                       << "seq:" << currentSeq;
        }
    }

    // ------------------ 其他NALU类型（忽略或警告） ------------------
    else {
        qWarning() << "[H264RtpReassembler] Unsupported NAL type, stream:" << streamName
                   << "seq:" << currentSeq << "type:" << static_cast<int>(nalType);
    }

    // ------------------ M=1 输出完整帧 ------------------
    if (pkt.marker && !_frame.isEmpty()) {
        qDebug() << "[H264RtpReassembler] Complete frame (marker set), stream:" << streamName
                 << "size:" << _frame.size() << "markerSeq:" << currentSeq;
        emit onFrameReady(streamName, _frame);
        _frame.clear();
    }
}

// 检查并保存SPS/PPS（利用原有_spsNalu/_ppsNalu字段）
void H264RtpReassembler::_checkAndSaveSpsPps(uint8_t nalType, const QByteArray &naluData)
{
    switch (nalType) {
        case 7: // SPS
            _spsNalu = naluData;
            qDebug() << "[H264RtpReassembler] SPS saved, size:" << naluData.size();
            break;
        case 8: // PPS
            _ppsNalu = naluData;
            qDebug() << "[H264RtpReassembler] PPS saved, size:" << naluData.size();
            break;
        default:
            break;
    }
}
