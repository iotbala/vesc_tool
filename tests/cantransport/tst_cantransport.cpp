// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QCanBus>
#include "vesccantransport.h"
#include "datatypes.h"
#include "packet.h"

class CanTransportTest : public QObject
{
    Q_OBJECT
private:
    static QCanBusFrame reply(int type, QByteArray payload, int target = 254)
    {
        QCanBusFrame frame(quint32(type << 8) | quint32(target), payload);
        frame.setExtendedFrameFormat(true);
        return frame;
    }

private slots:
    void pingHasReturnAddress()
    {
        const auto frame = VescCanTransport::ping(200);
        QVERIFY(frame.isValid());
        QVERIFY(frame.hasExtendedFrameFormat());
        QCOMPARE(frame.frameType(), QCanBusFrame::DataFrame);
        QCOMPARE(frame.frameId(), quint32((CAN_PACKET_PING << 8) | 200));
        QCOMPARE(frame.payload(), QByteArray::fromHex("fe"));
    }

    void shortCommandAndForwarding()
    {
        const auto frames = VescCanTransport::encode(QByteArray::fromHex("0601020304"), 2);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].frameId(), quint32((CAN_PACKET_PROCESS_SHORT_BUFFER << 8) | 2));
        QCOMPARE(frames[0].payload(), QByteArray::fromHex("fe000601020304"));
        QByteArray forwarded;
        forwarded.append(char(COMM_FORWARD_CAN));
        forwarded.append(char(200));
        forwarded.append(char(COMM_GET_VALUES));
        const auto fwd = VescCanTransport::encode(forwarded, 2);
        QCOMPARE(fwd.size(), 1);
        QCOMPARE(fwd[0].frameId() & 255, quint32(200));
        QVERIFY(VescCanTransport::encode(forwarded.left(2), 2).isEmpty());
        QVERIFY(VescCanTransport::encode(QByteArray(1, '\0'), 254).isEmpty());
    }

    void multiFrameReply_data()
    {
        QTest::addColumn<int>("length");
        for (int n : {1, 6, 7, 255, 256, 259, 260, 512, 513, 1024, 10000}) {
            QTest::newRow(qPrintable(QString::number(n))) << n;
        }
    }

    void multiFrameReply()
    {
        QFETCH(int, length);
        QByteArray packet(length, '\0');
        for (int i = 0; i < length; i++) {
            packet[i] = char(i & 255);
        }
        // Model a controller response using the same VESC framing and send-mode 1.
        auto frames = VescCanTransport::encode(packet, 2);
        QVERIFY(!frames.isEmpty());
        VescCanTransport receiver;
        QByteArray decoded;
        int offset = 0;
        for (auto frame : frames) {
            QVERIFY(frame.isValid());
            QVERIFY(frame.payload().size() <= 8);
            const int type = frame.frameId() >> 8;
            auto payload = frame.payload();
            if (type == CAN_PACKET_PROCESS_SHORT_BUFFER || type == CAN_PACKET_PROCESS_RX_BUFFER) {
                payload[0] = char(2);
                payload[1] = char(1);
            } else {
                const int indexBytes = type == CAN_PACKET_FILL_RX_BUFFER ? 1 : 2;
                const int index = indexBytes == 1 ? quint8(payload[0]) :
                                  (quint8(payload[0]) << 8) | quint8(payload[1]);
                QCOMPARE(index, offset);
                offset += payload.size() - indexBytes;
            }
            frame.setFrameId(quint32(type << 8) | 254);
            frame.setPayload(payload);
            const auto result = receiver.receive(frame);
            if (!result.packet.isEmpty()) {
                QVERIFY(decoded.isEmpty());
                decoded = result.packet;
            }
        }
        QCOMPARE(decoded, packet);
    }

    void independentFirmwareReply()
    {
        // A 10-byte firmware reply assembled independently of the encoder.
        const QByteArray packet = QByteArray::fromHex("00070154455354000102");
        VescCanTransport receiver;
        QVERIFY(receiver.receive(reply(CAN_PACKET_FILL_RX_BUFFER,
                    QByteArray::fromHex("0000070154455354"))).packet.isEmpty());
        receiver.receive(reply(CAN_PACKET_FILL_RX_BUFFER, QByteArray::fromHex("07000102")));
        const auto crc = Packet::crc16(reinterpret_cast<const unsigned char *>(packet.constData()), packet.size());
        QByteArray process = QByteArray::fromHex("0201000a");
        process.append(char(crc >> 8));
        process.append(char(crc));
        QCOMPARE(receiver.receive(reply(CAN_PACKET_PROCESS_RX_BUFFER, process)).packet, packet);
    }

    void rejectWrongAddressAndMalformedFrames()
    {
        VescCanTransport receiver;
        QCOMPARE(receiver.receive(reply(CAN_PACKET_PONG, QByteArray::fromHex("c801"))).nodeId, 200);
        QCOMPARE(receiver.receive(reply(CAN_PACKET_PONG, QByteArray())).nodeId, -1);
        QCOMPARE(receiver.receive(reply(CAN_PACKET_PONG, QByteArray::fromHex("02"), 0)).nodeId, -1);
        auto standard = reply(CAN_PACKET_PROCESS_SHORT_BUFFER, QByteArray::fromHex("020104"));
        standard.setExtendedFrameFormat(false);
        QVERIFY(receiver.receive(standard).packet.isEmpty());
        for (int type : {CAN_PACKET_PROCESS_RX_BUFFER, CAN_PACKET_FILL_RX_BUFFER,
                        CAN_PACKET_FILL_RX_BUFFER_LONG, CAN_PACKET_PROCESS_SHORT_BUFFER}) {
            for (int len = 0; len < 3; len++) {
                QVERIFY(receiver.receive(reply(type, QByteArray(len, '\0'))).packet.isEmpty());
            }
        }
        QVERIFY(receiver.receive(reply(CAN_PACKET_PROCESS_RX_BUFFER, QByteArray::fromHex("0201ffff0000"))).packet.isEmpty());
    }

    void rejectLostFragmentAndBadCrc()
    {
        VescCanTransport receiver;
        receiver.receive(reply(CAN_PACKET_FILL_RX_BUFFER, QByteArray::fromHex("0001020304050607")));
        receiver.receive(reply(CAN_PACKET_FILL_RX_BUFFER, QByteArray::fromHex("0e0809"))); // gap
        QVERIFY(receiver.receive(reply(CAN_PACKET_PROCESS_RX_BUFFER, QByteArray::fromHex("020100090000"))).packet.isEmpty());
        receiver.receive(reply(CAN_PACKET_FILL_RX_BUFFER, QByteArray::fromHex("0001020304050607")));
        QVERIFY(receiver.receive(reply(CAN_PACKET_PROCESS_RX_BUFFER, QByteArray::fromHex("020100070000"))).packet.isEmpty());
        QCOMPARE(receiver.receive(reply(CAN_PACKET_PROCESS_SHORT_BUFFER, QByteArray::fromHex("020104"))).packet,
                 QByteArray::fromHex("04"));
    }

    void peakPluginAvailable()
    {
#ifdef Q_OS_WIN
        QVERIFY2(QCanBus::instance()->plugins().contains("peakcan"), "Deploy Qt's canbus/qtpeakcanbus.dll");
        QString error;
        const auto devices = QCanBus::instance()->availableDevices("peakcan", &error);
        qInfo() << "PCAN enumeration:" << devices.size() << "adapter(s);" << error;
        for (const auto &device : devices) {
            qInfo() << device.name() << device.description();
        }
#endif
    }
};

QTEST_GUILESS_MAIN(CanTransportTest)
#include "tst_cantransport.moc"
