// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/input/QcGamepadResponse.h"
#include "clientcore/input/QcGamepadState.h"
#include "clientcore/input/QcGamepadTypeDetection.h"
#include "clientcore/input/QcVirtualGamepadSource.h"
#include "clientcore/session/QcClientSession.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

float length(QcStickVector v)
{
    return std::hypot(v.x, v.y);
}

// Runs control commands of the gamepad group without a session and collects one response per line.
class GamepadCommandRunner
{
public:
    GamepadCommandRunner()
    {
        m_output.open(QIODevice::ReadWrite);
    }

    // The responses written for this request.
    QList<QJsonObject> run(const QByteArray &requestJson)
    {
        const qint64 start = m_output.size();
        const QJsonObject request = QJsonDocument::fromJson(requestJson).object();
        const QcControlCommandTable::Handler *handler = m_table.find(request.value(u"cmd"_s).toString());
        if (handler) {
            (*handler)(request);
        }

        QList<QJsonObject> responses;
        const QList<QByteArray> lines = m_output.data().mid(start).split('\n');
        for (const QByteArray &line : lines) {
            if (!line.isEmpty()) {
                responses.append(QJsonDocument::fromJson(line).object());
            }
        }
        return responses;
    }

private:
    QBuffer m_output;
    QcControlContext m_context{nullptr, &m_output};
    QcControlCommandTable m_table{m_context};
};

} // namespace

// The backend-independent gamepad model (decision E-540): normalised states, the radial dead zone
// with its response curve, the detection of the pad family and the virtual source.
class TestGamepadInput : public QObject
{
    Q_OBJECT

private slots:
    // Sticks are clamped to [-1, 1], triggers to [0, 1], NaN reads as 0.
    void stateClampsAxes()
    {
        QcGamepadState state;
        state.setAxis(QcGamepadAxis::LeftX, 1.7f);
        state.setAxis(QcGamepadAxis::LeftY, -3.0f);
        state.setAxis(QcGamepadAxis::LeftTrigger, -0.4f);
        state.setAxis(QcGamepadAxis::RightTrigger, 2.0f);
        state.setAxis(QcGamepadAxis::RightX, std::numeric_limits<float>::quiet_NaN());
        QCOMPARE(state.axis(QcGamepadAxis::LeftX), 1.0f);
        QCOMPARE(state.axis(QcGamepadAxis::LeftY), -1.0f);
        QCOMPARE(state.axis(QcGamepadAxis::LeftTrigger), 0.0f);
        QCOMPARE(state.axis(QcGamepadAxis::RightTrigger), 1.0f);
        QCOMPARE(state.axis(QcGamepadAxis::RightX), 0.0f);

        state.setPressed(QcGamepadButton::North, true);
        QVERIFY(state.pressed(QcGamepadButton::North));
        QVERIFY(!state.pressed(QcGamepadButton::South));
        QVERIFY(state != QcGamepadState{});
    }

    // Every button, axis and type has a distinct name that leads back to it.
    void namesRoundTrip()
    {
        QStringList seen;
        for (std::size_t i = 0; i < qcGamepadButtonCount; ++i) {
            const QString name = QcGamepadNames::button(static_cast<QcGamepadButton>(i));
            QVERIFY(!name.isEmpty() && !seen.contains(name));
            seen.append(name);
            QCOMPARE(QcGamepadNames::buttonFromName(name), std::optional(QcGamepadButton(i)));
        }
        for (std::size_t i = 0; i < qcGamepadAxisCount; ++i) {
            const QString name = QcGamepadNames::axis(static_cast<QcGamepadAxis>(i));
            QCOMPARE(QcGamepadNames::axisFromName(name), std::optional(QcGamepadAxis(i)));
        }
        for (const QcGamepadType type : {QcGamepadType::Generic, QcGamepadType::Xbox, QcGamepadType::PlayStation, QcGamepadType::SwitchPro}) {
            QCOMPARE(QcGamepadNames::typeFromName(QcGamepadNames::type(type)), std::optional(type));
        }
        QVERIFY(!QcGamepadNames::buttonFromName(u"a"_s));
        QVERIFY(!QcGamepadNames::axisFromName(u"LeftX"_s));
    }

    // Inside the radial dead zone nothing moves, also diagonally where each axis alone is small
    // but the length is not.
    void deadZoneIsRadial()
    {
        const QcGamepadResponse response{.deadZone = 0.2f, .outerZone = 0.0f, .exponent = 1.0f};
        QCOMPARE(QcGamepadCurve::stick({0.19f, 0.0f}, response), QcStickVector{});
        QCOMPARE(QcGamepadCurve::stick({0.1f, -0.1f}, response), QcStickVector{});

        // 0.15 on both axes is 0.212 long: outside a radial dead zone of 0.2, although each axis
        // alone would be inside an axial one.
        const QcStickVector diagonal = QcGamepadCurve::stick({0.15f, 0.15f}, response);
        QVERIFY(diagonal.x > 0.0f && diagonal.y > 0.0f);
        QVERIFY(qFuzzyCompare(diagonal.x, diagonal.y));
    }

    // Past the dead zone the output starts at 0 (no jump), rises monotonically, keeps the
    // direction and reaches 1 at the outer zone.
    void outputIsContinuousAndKeepsDirection()
    {
        const QcGamepadResponse response{.deadZone = 0.15f, .outerZone = 0.05f, .exponent = 1.5f};
        const float justOutside = length(QcGamepadCurve::stick({0.1501f, 0.0f}, response));
        QVERIFY2(justOutside < 0.001f, qPrintable(QString::number(justOutside)));

        float previous = 0.0f;
        for (int step = 16; step <= 100; ++step) {
            const float raw = static_cast<float>(step) / 100.0f;
            const float out = length(QcGamepadCurve::stick({raw * 0.6f, raw * -0.8f}, response));
            QVERIFY2(out >= previous - 1e-6f, qPrintable(u"%1 at %2"_s.arg(out).arg(raw)));
            previous = out;
        }
        QVERIFY(qFuzzyCompare(previous, 1.0f));

        const QcStickVector v = QcGamepadCurve::stick({0.3f, -0.4f}, response);
        QVERIFY(std::abs(v.x / v.y - (0.3f / -0.4f)) < 1e-5f);
        QVERIFY(qFuzzyCompare(length(QcGamepadCurve::stick({0.96f, 0.0f}, response)), 1.0f));
        QVERIFY(qFuzzyCompare(length(QcGamepadCurve::stick({1.0f, 1.0f}, response)), 1.0f));
    }

    // The exponent bends the curve: linear at 1, finer near the centre above 1, the middle of the
    // live range gives 0.5^exponent.
    void exponentShapesTheCurve()
    {
        const auto at = [](float raw, float exponent) {
            return QcGamepadCurve::stick({raw, 0.0f}, {.deadZone = 0.1f, .outerZone = 0.1f, .exponent = exponent}).x;
        };
        QVERIFY(std::abs(at(0.5f, 1.0f) - 0.5f) < 1e-5f);
        QVERIFY(std::abs(at(0.5f, 2.0f) - 0.25f) < 1e-5f);
        QVERIFY(at(0.3f, 2.0f) < at(0.3f, 1.0f));
        QVERIFY(std::abs(at(-0.5f, 2.0f) + 0.25f) < 1e-5f);
    }

    // Triggers: a dead zone at rest, full at the outer zone; settings out of range are clamped.
    void triggersAndSanitizing()
    {
        const QcGamepadResponse response{.deadZone = 0.15f, .outerZone = 0.05f, .exponent = 1.0f, .triggerDeadZone = 0.1f};
        QCOMPARE(QcGamepadCurve::trigger(0.05f, response), 0.0f);
        QCOMPARE(QcGamepadCurve::trigger(0.96f, response), 1.0f);
        QVERIFY(std::abs(QcGamepadCurve::trigger(0.525f, response) - 0.5f) < 1e-5f);
        QCOMPARE(QcGamepadCurve::trigger(-1.0f, response), 0.0f);

        const QcGamepadResponse wild{.deadZone = 7.0f, .outerZone = -1.0f, .exponent = 0.0f, .triggerDeadZone = std::numeric_limits<float>::quiet_NaN()};
        const QcGamepadResponse sane = wild.sanitized();
        QCOMPARE(sane.deadZone, 0.9f);
        QCOMPARE(sane.outerZone, 0.0f);
        // The outer zone leaves at least 5 % of the travel after the larger dead zone.
        const QcGamepadResponse crowded = QcGamepadResponse{.deadZone = 0.8f, .outerZone = 0.5f, .triggerDeadZone = 0.85f}.sanitized();
        QVERIFY(std::abs(crowded.outerZone - 0.1f) < 1e-5f);
        QCOMPARE(sane.exponent, 0.2f);
        QCOMPARE(sane.triggerDeadZone, QcGamepadResponse{}.triggerDeadZone);
        // Even an extreme dead zone gives full output at full deflection and nothing invalid.
        QVERIFY(qFuzzyCompare(QcGamepadCurve::stick({1.0f, 0.0f}, wild).x, 1.0f));
        QCOMPARE(QcGamepadCurve::stick({std::numeric_limits<float>::quiet_NaN(), 0.0f}, wild), QcStickVector{});
        // An infinite component counts as the rim, not as NaN.
        const float infinity = std::numeric_limits<float>::infinity();
        QCOMPARE(QcGamepadCurve::stick({-infinity, 0.0f}, response), (QcStickVector{-1.0f, 0.0f}));
        const QcStickVector diagonal = QcGamepadCurve::stick({infinity, infinity}, response);
        QVERIFY(std::isfinite(diagonal.x) && std::abs(std::hypot(diagonal.x, diagonal.y) - 1.0f) < 1e-5f);
    }

    // apply() changes sticks and triggers and leaves buttons alone.
    void applyKeepsButtons()
    {
        QcGamepadState raw;
        raw.setPressed(QcGamepadButton::Start, true);
        raw.setAxis(QcGamepadAxis::RightX, 0.05f);
        raw.setAxis(QcGamepadAxis::LeftY, -1.0f);
        raw.setAxis(QcGamepadAxis::RightTrigger, 1.0f);
        const QcGamepadState shaped = QcGamepadCurve::apply(raw, QcGamepadResponse{});
        QVERIFY(shaped.pressed(QcGamepadButton::Start));
        QCOMPARE(shaped.axis(QcGamepadAxis::RightX), 0.0f);
        QVERIFY(qFuzzyCompare(shaped.axis(QcGamepadAxis::LeftY), -1.0f));
        QCOMPARE(shaped.axis(QcGamepadAxis::RightTrigger), 1.0f);
    }

    // The family from vendor ids first, then from the name.
    void detectsTheType()
    {
        using QcGamepadTypeDetection::detect;
        QCOMPARE(detect(0x045e, 0x0b13, u"Controller"_s), QcGamepadType::Xbox);
        QCOMPARE(detect(0x054c, 0x0ce6, u"Wireless Controller"_s), QcGamepadType::PlayStation);
        QCOMPARE(detect(0x057e, 0x2009, u"Pro Controller"_s), QcGamepadType::SwitchPro);
        QCOMPARE(detect(0x057e, 0x2006, u"Joy-Con (L)"_s), QcGamepadType::Generic);
        // SDL's own names: a single Joy-Con is Generic by Nintendo's vendor id and by its name.
        QCOMPARE(detect(0x057e, 0x2006, u"Nintendo Switch Joy-Con (L)"_s), QcGamepadType::Generic);
        QCOMPARE(detect(0x057e, 0x2007, u"Nintendo Switch Joy-Con (R)"_s), QcGamepadType::Generic);
        QCOMPARE(detect(0, 0, u"Nintendo Switch Joy-Con (L)"_s), QcGamepadType::Generic);
        QCOMPARE(detect(0x057e, 0x200e, u"Nintendo Switch Joy-Con (L/R)"_s), QcGamepadType::SwitchPro);
        QCOMPARE(detect(0x057e, 0x2009, u"Nintendo Switch Pro Controller"_s), QcGamepadType::SwitchPro);
        QCOMPARE(detect(0, 0, u"Nintendo Switch Joy-Con (L/R)"_s), QcGamepadType::SwitchPro);
        QCOMPARE(detect(0x0f0d, 0x00c1, u"HORIPAD for Nintendo Switch"_s), QcGamepadType::SwitchPro);
        QCOMPARE(detect(0x0e6f, 0x02a1, u"PDP Xbox Wired Controller"_s), QcGamepadType::Xbox);
        QCOMPARE(detect(0x2dc8, 0x6101, u"8BitDo SN30 Pro"_s), QcGamepadType::Generic);
        QCOMPARE(detect(0, 0, u"DualSense Wireless Controller"_s), QcGamepadType::PlayStation);
    }

    // The virtual source: connect, change, disconnect with the matching signals; no signal for a
    // state that did not change; ids are not reused.
    void virtualSourceEvents()
    {
        QcVirtualGamepadSource source;
        QVERIFY(source.isAvailable());
        QCOMPARE(source.backendName(), u"virtual"_s);
        QSignalSpy connected(&source, &QcGamepadSource::padConnected);
        QSignalSpy disconnected(&source, &QcGamepadSource::padDisconnected);
        QSignalSpy changed(&source, &QcGamepadSource::stateChanged);

        const int first = source.connectPad(u"Pad A"_s, QcGamepadType::PlayStation);
        const int second = source.connectPad(u"Pad B"_s, QcGamepadType::Xbox);
        QCOMPARE(connected.count(), 2);
        QCOMPARE(connected.at(0).at(0).toInt(), first);
        QVERIFY(first > 0 && second != first);
        QCOMPARE(source.pads().size(), 2);
        QCOMPARE(source.pads().at(0), (QcGamepadInfo{.id = first, .name = u"Pad A"_s, .type = QcGamepadType::PlayStation, .guid = {}, .rumble = true}));

        QVERIFY(source.setPressed(first, QcGamepadButton::South, true));
        QVERIFY(source.setAxis(first, QcGamepadAxis::LeftX, 0.5f));
        QCOMPARE(changed.count(), 2);
        QVERIFY(source.setPressed(first, QcGamepadButton::South, true));
        QCOMPARE(changed.count(), 2);
        QVERIFY(source.state(first).pressed(QcGamepadButton::South));
        QCOMPARE(source.state(first).axis(QcGamepadAxis::LeftX), 0.5f);
        QCOMPARE(source.state(second), QcGamepadState{});

        QVERIFY(source.disconnectPad(first));
        QVERIFY(!source.disconnectPad(first));
        QVERIFY(!source.setPressed(first, QcGamepadButton::East, true));
        QCOMPARE(disconnected.count(), 1);
        QCOMPARE(disconnected.at(0).at(0).toInt(), first);
        QCOMPARE(source.state(first), QcGamepadState{});

        const int third = source.connectPad(u"Pad C"_s, QcGamepadType::Generic);
        QVERIFY(third != first && third != second);
    }

    // gamepadSet answers every request exactly once and rejects `buttons` or `axes` that are not
    // objects instead of ignoring them.
    void gamepadSetChecksContainerTypes()
    {
        GamepadCommandRunner runner;
        const QList<QJsonObject> connected = runner.run(R"({"cmd":"gamepadConnect"})");
        QCOMPARE(connected.size(), 1);
        QVERIFY(connected.at(0).value(u"ok"_s).toBool());
        const int pad = connected.at(0).value(u"pad"_s).toInt();

        const auto failure = [&](const QByteArray &request) {
            const QList<QJsonObject> responses = runner.run(request);
            return responses.size() == 1 && !responses.at(0).value(u"ok"_s).toBool(true) ? responses.at(0).value(u"error"_s).toString() : QString();
        };
        const QByteArray prefix = R"({"cmd":"gamepadSet","pad":)" + QByteArray::number(pad);
        QVERIFY(failure(prefix + R"(,"buttons":["south"]})").contains(u"'buttons' must be an object"_s));
        QVERIFY(failure(prefix + R"(,"buttons":true})").contains(u"'buttons' must be an object"_s));
        QVERIFY(failure(prefix + R"(,"axes":0.5})").contains(u"'axes' must be an object"_s));
        QVERIFY(failure(prefix + R"(,"axes":[0.5]})").contains(u"'axes' must be an object"_s));
        QVERIFY(failure(prefix + R"(,"buttons":{"south":1}})").contains(u"not true/false"_s));
        QVERIFY(failure(R"({"cmd":"gamepadSet","pad":99})").contains(u"No virtual gamepad 99"_s));

        // A failed request changes nothing; a valid one answers once with the new state.
        const QList<QJsonObject> set = runner.run(prefix + R"(,"buttons":{"south":true},"axes":{"leftX":-2}})");
        QCOMPARE(set.size(), 1);
        QVERIFY(set.at(0).value(u"ok"_s).toBool());
        QCOMPARE(set.at(0).value(u"pressed"_s).toArray(), QJsonArray{u"south"_s});
        QCOMPARE(set.at(0).value(u"axes"_s).toObject().value(u"leftX"_s).toDouble(), -1.0);
    }
};

QTEST_GUILESS_MAIN(TestGamepadInput)
#include "test_gamepad_input.moc"
