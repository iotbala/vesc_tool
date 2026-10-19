// SPDX-License-Identifier: GPL-3.0-or-later
// Based on VESC Tool's CAN transport, Copyright 2016-2020 Benjamin Vedder.
#ifndef VESCCANTRANSPORT_H
#define VESCCANTRANSPORT_H

#include <QCanBusFrame>
#include <QVector>

// Classic CAN packet transport shared by SocketCAN and PEAK PCAN.
class VescCanTransport
{
public:
    enum { ToolNodeId = 254, MaxPacketSize = 10000 };
    struct Received {
        QByteArray packet;
        int nodeId = -1;
    };

    static QCanBusFrame ping(int nodeId);
    static QVector<QCanBusFrame> encode(QByteArray packet, int nodeId);
    Received receive(const QCanBusFrame &frame);
    void reset();

private:
    QByteArray mRxBuffer;
};

#endif
