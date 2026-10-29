// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/QcAudioRoute.h"

#include <QTest>

namespace {

using Action = QcAudioRoute::Decision::Action;

QcAudioRoute::Decision decide(const char *current, const QList<QByteArray> &outputs, const char *defaultOutput)
{
    return QcAudioRoute::decide(QByteArray(current), outputs, QByteArray(defaultOutput));
}

} // namespace

// Which output device the game follows when the system's devices change, on a fake device list.
class TestAudioRoute : public QObject
{
    Q_OBJECT

private slots:
    void staysOnTheDefaultOutput()
    {
        QCOMPARE(decide("speakers", {"speakers", "hdmi"}, "speakers"), QcAudioRoute::Decision{});
        // A device appearing that does not become the default changes nothing.
        QCOMPARE(decide("speakers", {"speakers", "hdmi", "usb"}, "speakers"), QcAudioRoute::Decision{});
    }

    void followsANewDefault()
    {
        // A headset switched on becomes the system's default.
        QCOMPARE(decide("speakers", {"speakers", "headset"}, "headset"), (QcAudioRoute::Decision{Action::Switch, "headset"}));
        // A default the list does not show (yet) is not followed.
        QCOMPARE(decide("speakers", {"speakers"}, "headset"), QcAudioRoute::Decision{});
    }

    void movesOnWhenTheDeviceGoesAway()
    {
        QCOMPARE(decide("headset", {"speakers"}, "speakers"), (QcAudioRoute::Decision{Action::Switch, "speakers"}));
        // No default reported: any device left is better than silence.
        QCOMPARE(decide("headset", {"hdmi"}, ""), (QcAudioRoute::Decision{Action::Switch, "hdmi"}));
        QCOMPARE(decide("headset", {}, ""), (QcAudioRoute::Decision{Action::Close, {}}));
    }

    void opensTheFirstDeviceThatAppears()
    {
        // Started without a device: the first one plugged in is used.
        QCOMPARE(decide("", {}, ""), QcAudioRoute::Decision{});
        QCOMPARE(decide("", {"headset"}, "headset"), (QcAudioRoute::Decision{Action::Switch, "headset"}));
        QCOMPARE(decide("", {"headset"}, ""), (QcAudioRoute::Decision{Action::Switch, "headset"}));
    }
};

QTEST_GUILESS_MAIN(TestAudioRoute)
#include "test_audio_route.moc"
