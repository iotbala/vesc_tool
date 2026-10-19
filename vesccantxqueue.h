// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef VESCCANTXQUEUE_H
#define VESCCANTXQUEUE_H

#include <QObject>
#include <QCanBusDevice>
#include <QElapsedTimer>
#include <QPointer>
#include <QQueue>
#include <QTimer>

// Submit CAN packets in order without running a nested event loop on the GUI thread.
class VescCanTxQueue : public QObject
{
    Q_OBJECT
public:
    enum { MaxQueuedFrames = 2048, StallTimeoutMs = 500 };
    explicit VescCanTxQueue(QObject *parent = nullptr);
    void setDevice(QCanBusDevice *device);
    bool enqueue(const QVector<QCanBusFrame> &frames);
    int pendingFrames() const;

signals:
    void failed(const QString &message);

private:
    void pump();
    void fail(const QString &message);
    QPointer<QCanBusDevice> mDevice;
    QQueue<QCanBusFrame> mFrames;
    QTimer mTimer;
    QElapsedTimer mProgress;
};

#endif
