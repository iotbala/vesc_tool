// SPDX-License-Identifier: GPL-3.0-or-later
// Based on VESC Tool's CAN transport, Copyright 2016-2020 Benjamin Vedder.
#include "vesccantransport.h"
#include "datatypes.h"
#include "packet.h"

namespace {
QCanBusFrame makeFrame(int type, int nodeId, const QByteArray &payload)
{
    QCanBusFrame frame(quint32(type << 8) | quint32(nodeId), payload);
    frame.setExtendedFrameFormat(true);
    frame.setFrameType(QCanBusFrame::DataFrame);
    return frame;
}

quint16 crc16(const QByteArray &data)
{
    return Packet::crc16(reinterpret_cast<const unsigned char *>(data.constData()), data.size());
}
}

QCanBusFrame VescCanTransport::ping(int nodeId)
{
    return makeFrame(CAN_PACKET_PING, nodeId, QByteArray(1, char(ToolNodeId)));
}

QVector<QCanBusFrame> VescCanTransport::encode(QByteArray packet, int nodeId)
{
    QVector<QCanBusFrame> frames;
    if (packet.isEmpty() || packet.size() > MaxPacketSize) {
        return frames;
    }
    if (quint8(packet.at(0)) == COMM_FORWARD_CAN) {
        if (packet.size() < 3) {
            return frames;
        }
        nodeId = quint8(packet.at(1));
        packet.remove(0, 2);
    }
    if (nodeId < 0 || nodeId >= ToolNodeId) {
        return frames;
    }

    if (packet.size() <= 6) {
        packet.prepend(char(0)); // Process the command and return its reply over CAN.
        packet.prepend(char(ToolNodeId));
        frames.append(makeFrame(CAN_PACKET_PROCESS_SHORT_BUFFER, nodeId, packet));
        return frames;
    }

    int offset = 0;
    while (offset < packet.size()) {
        QByteArray payload;
        const bool longOffset = offset > 255;
        if (longOffset) {
            payload.append(char(offset >> 8));
        }
        payload.append(char(offset & 0xff));
        const auto chunk = packet.mid(offset, longOffset ? 6 : 7);
        payload.append(chunk);
        frames.append(makeFrame(longOffset ? CAN_PACKET_FILL_RX_BUFFER_LONG :
                                             CAN_PACKET_FILL_RX_BUFFER, nodeId, payload));
        offset += chunk.size();
    }

    const quint16 crc = crc16(packet);
    QByteArray payload;
    payload.append(char(ToolNodeId));
    payload.append(char(0));
    payload.append(char(packet.size() >> 8));
    payload.append(char(packet.size() & 0xff));
    payload.append(char(crc >> 8));
    payload.append(char(crc & 0xff));
    frames.append(makeFrame(CAN_PACKET_PROCESS_RX_BUFFER, nodeId, payload));
    return frames;
}

VescCanTransport::Received VescCanTransport::receive(const QCanBusFrame &frame)
{
    Received result;
    if (!frame.isValid() || !frame.hasExtendedFrameFormat() ||
            frame.frameType() != QCanBusFrame::DataFrame || frame.hasFlexibleDataRateFormat() ||
            (frame.frameId() & 0xff) != ToolNodeId) {
        return result;
    }

    const auto payload = frame.payload();
    const int type = int(frame.frameId() >> 8);
    if (type == CAN_PACKET_PONG) {
        if (!payload.isEmpty() && quint8(payload.at(0)) < ToolNodeId) {
            result.nodeId = quint8(payload.at(0));
        }
    } else if (type == CAN_PACKET_PROCESS_SHORT_BUFFER) {
        if (payload.size() >= 3 && quint8(payload.at(1)) == 1) {
            result.packet = payload.mid(2);
        }
    } else if (type == CAN_PACKET_FILL_RX_BUFFER || type == CAN_PACKET_FILL_RX_BUFFER_LONG) {
        const int indexBytes = type == CAN_PACKET_FILL_RX_BUFFER ? 1 : 2;
        if (payload.size() <= indexBytes) {
            reset();
            return result;
        }
        const int offset = indexBytes == 1 ? quint8(payload.at(0)) :
                          (quint8(payload.at(0)) << 8) | quint8(payload.at(1));
        if (offset == 0) {
            reset();
        }
        if (offset != mRxBuffer.size() || offset + payload.size() - indexBytes > MaxPacketSize) {
            reset();
            return result;
        }
        mRxBuffer.append(payload.mid(indexBytes));
    } else if (type == CAN_PACKET_PROCESS_RX_BUFFER) {
        if (payload.size() == 6) {
            const int length = (quint8(payload.at(2)) << 8) | quint8(payload.at(3));
            const quint16 crc = (quint8(payload.at(4)) << 8) | quint8(payload.at(5));
            if (quint8(payload.at(1)) == 1 && length > 0 && length == mRxBuffer.size() &&
                    crc16(mRxBuffer) == crc) {
                result.packet = mRxBuffer;
            }
        }
        reset();
    }
    return result;
}

void VescCanTransport::reset()
{
    mRxBuffer.clear();
}
