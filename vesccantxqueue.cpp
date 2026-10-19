// SPDX-License-Identifier: GPL-3.0-or-later
#include "vesccantxqueue.h"

VescCanTxQueue::VescCanTxQueue(QObject *parent) : QObject(parent)
{
    mTimer.setInterval(2);
    mTimer.setTimerType(Qt::PreciseTimer);
    connect(&mTimer, &QTimer::timeout, this, &VescCanTxQueue::pump);
}

void VescCanTxQueue::setDevice(QCanBusDevice *device)
{
    mTimer.stop();
    mFrames.clear();
    mProgress.invalidate();
    mDevice = device;
}

int VescCanTxQueue::pendingFrames() const
{
    return mFrames.size();
}

bool VescCanTxQueue::enqueue(const QVector<QCanBusFrame> &frames)
{
    if (!mDevice || mDevice->state() != QCanBusDevice::ConnectedState) {
        fail(tr("CAN adapter is disconnected."));
        return false;
    }
    if (frames.size() > MaxQueuedFrames - mFrames.size()) {
        // Reject the whole packet; never send only the first part of a command.
        fail(tr("CAN transmit queue is full. Reconnect before sending more commands."));
        return false;
    }
    for (const auto &frame : frames) {
        mFrames.enqueue(frame);
    }
    if (!frames.isEmpty() && !mTimer.isActive()) {
        mProgress.start();
        mTimer.start();
    }
    return true;
}

void VescCanTxQueue::fail(const QString &message)
{
    mTimer.stop();
    mFrames.clear();
    emit failed(message);
}

void VescCanTxQueue::pump()
{
    if (!mDevice || mDevice->state() != QCanBusDevice::ConnectedState) {
        fail(tr("CAN adapter disconnected during transmission."));
        return;
    }
    // Qt's PeakCAN plugin is buffered. Keep at most one frame in that backend
    // queue, and wait for progress by returning to the main event loop.
    if (mDevice->framesToWrite() > 0) {
        if (mProgress.elapsed() >= StallTimeoutMs) {
            fail(tr("CAN transmission stalled. Check bus wiring and bitrate, then reconnect."));
        }
        return;
    }
    if (mFrames.isEmpty()) {
        mTimer.stop();
        return;
    }
    const auto frame = mFrames.dequeue();
    if (!mDevice->writeFrame(frame)) {
        const auto message = mDevice ? mDevice->errorString() : tr("adapter disconnected");
        fail(tr("CAN transmit failed: %1").arg(message));
        return;
    }
    mProgress.restart();
}
