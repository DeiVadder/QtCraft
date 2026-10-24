#include "app/models/QcUiScaling.h"
#include "clientcore/settings/QcUiScale.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// The UI scale (decision E-725): the steps and their names, the factor Auto picks, the limit a
// window sets, the QML singleton UiScaling with its warnings, and Ui.px() following the factor.
class TestUiScale : public QObject
{
    Q_OBJECT

private slots:
    // Every step has a name that comes back; unknown names do not; Auto has no fixed factor.
    void stepsAndNames()
    {
        using QcUiScale::Step;
        for (const Step step : {Step::Auto, Step::Percent75, Step::Percent100, Step::Percent125, Step::Percent150, Step::Percent175,
                                Step::Percent200}) {
            QCOMPARE(QcUiScale::fromName(QcUiScale::name(step)), step);
        }
        QCOMPARE(QcUiScale::name(Step::Auto), u"auto"_s);
        QCOMPARE(QcUiScale::name(Step::Percent125), u"125"_s);
        QCOMPARE(QcUiScale::fromName(u"133"_s), std::nullopt);
        QCOMPARE(QcUiScale::fromName(u"Auto"_s), std::nullopt);
        QCOMPARE(QcUiScale::fixedFactor(Step::Auto), std::nullopt);
        QCOMPARE(QcUiScale::fixedFactor(Step::Percent75), 0.75);
        QCOMPARE(QcUiScale::fixedFactor(Step::Percent200), 2.0);
        QCOMPARE(QcUiScale::defaultStep, Step::Percent100);
    }

    // The window's factor (decision E-875): its logical size against 1550 x 1000 (a MacBook's
    // screen, 1.0) in eighths, never below 100 % or above 300 %; every step is multiplied with it.
    void windowFactorFollowsTheWindow()
    {
        QCOMPARE(QcUiScale::windowFactor({1280, 720}), 1.0);
        QCOMPARE(QcUiScale::windowFactor({1024, 640}), 1.0);
        QCOMPARE(QcUiScale::windowFactor({1512, 982}), 1.0);
        QCOMPARE(QcUiScale::windowFactor({1920, 1080}), 1.125);
        QCOMPARE(QcUiScale::windowFactor({2560, 1440}), 1.5);
        QCOMPARE(QcUiScale::windowFactor({3840, 2160}), 2.125);
        QCOMPARE(QcUiScale::windowFactor({7680, 4320}), 3.0);
        // The smaller side decides: a wide, low window stays at 100 %.
        QCOMPARE(QcUiScale::windowFactor({3440, 1000}), 1.0);
        QCOMPARE(QcUiScale::windowFactor({3440, 1440}), 1.5);
        QCOMPARE(QcUiScale::windowFactor({0, 0}), 1.0);
        QCOMPARE(QcUiScale::windowFactor({qQNaN(), 720}), 1.0);
        QCOMPARE(QcUiScale::requestedFactor(QcUiScale::Step::Percent100, {2560, 1440}), 1.5);
        QCOMPARE(QcUiScale::requestedFactor(QcUiScale::Step::Percent200, {2560, 1440}), 3.0);
        QCOMPARE(QcUiScale::requestedFactor(QcUiScale::Step::Percent75, {2560, 1440}), 1.125);
        // Auto, from older settings files, is 100 %.
        QCOMPARE(QcUiScale::requestedFactor(QcUiScale::Step::Auto, {2560, 1440}), 1.5);
        QCOMPARE(QcUiScale::requestedFactor(QcUiScale::Step::Percent175, {1024, 640}), 1.75);
    }

    // A factor is limited to what lets the design size fit (in eighths); 100 % and less always stay.
    void fittingLimitsTheFactor()
    {
        const QSizeF screens = QcUiScale::screenDesignSize;
        const QSizeF hud = QcUiScale::hudDesignSize;
        QCOMPARE(QcUiScale::fittedFactor(2.0, {1280, 720}, screens), 1.125);
        QCOMPARE(QcUiScale::fittedFactor(2.0, {1280, 720}, hud), 2.0);
        QCOMPARE(QcUiScale::fittedFactor(2.0, {2048, 1280}, screens), 2.0);
        QCOMPARE(QcUiScale::fittedFactor(1.5, {1366, 768}, screens), 1.125);
        QCOMPARE(QcUiScale::fittedFactor(1.0, {1024, 640}, screens), 1.0);
        QCOMPARE(QcUiScale::fittedFactor(1.25, {800, 600}, screens), 1.0);
        QCOMPARE(QcUiScale::fittedFactor(0.75, {800, 600}, screens), 0.75);
        QCOMPARE(QcUiScale::fittedFactor(0.75, {3840, 2160}, screens), 0.75);
        QCOMPARE(QcUiScale::fittedFactor(qQNaN(), {1280, 720}, screens), 1.0);
        QCOMPARE(QcUiScale::fittedFactor(2.0, {0, 0}, screens), 1.0);
    }

    // The singleton computes both factors from its inputs, tells only when a factor changes, and
    // warns about values it refuses.
    void scalingFollowsItsInputs()
    {
        QcUiScaling scaling;
        QCOMPARE(scaling.setting(), QcUiScaling::Step::Percent100);
        QCOMPARE(scaling.scale(), 1.0);
        QSignalSpy factors(&scaling, &QcUiScaling::factorsChanged);

        scaling.setWindowSize({1280, 720});
        scaling.setSetting(QcUiScaling::Step::Percent200);
        QCOMPARE(scaling.requested(), 2.0);
        QCOMPARE(scaling.scale(), 1.125);
        QCOMPARE(scaling.hudScale(), 2.0);
        QCOMPARE(factors.count(), 1);
        // The controls' font grows with the screens.
        const QFont font = QGuiApplication::font();
        if (font.pointSizeF() > 0.0) {
            QCOMPARE(scaling.font().pointSizeF(), font.pointSizeF() * 1.125);
        } else {
            QCOMPARE(scaling.font().pixelSize(), qRound(font.pixelSize() * 1.125));
        }

        // A slightly larger window stays within the same eighth: no new factors.
        scaling.setWindowSize({1290, 725});
        QCOMPARE(factors.count(), 1);
        // 200 % of the window's 1.5: the screens as large as fits (2560 / 1024 = 2.5, 1440 / 640 = 2.25).
        scaling.setWindowSize({2560, 1440});
        QCOMPARE(scaling.requested(), 3.0);
        QCOMPARE(scaling.scale(), 2.25);
        QCOMPARE(factors.count(), 2);

        scaling.setSetting(QcUiScaling::Step::Auto);
        scaling.setWindowSize({1920, 1080});
        QCOMPARE(scaling.requested(), 1.125);
        QCOMPARE(scaling.scale(), 1.125);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"UI scale: ignoring unknown step 12"_s));
        scaling.setSetting(static_cast<QcUiScaling::Step>(12));
        QCOMPARE(scaling.setting(), QcUiScaling::Step::Auto);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"UI scale: ignoring window size"_s));
        scaling.setWindowSize({-5, 100});
        QCOMPARE(scaling.windowSize(), QSizeF(1920, 1080));
        // An empty window (not shown yet) keeps the last size silently.
        scaling.setWindowSize({0, 0});
        QCOMPARE(scaling.windowSize(), QSizeF(1920, 1080));
    }

    // Sizes written with Ui.px() and Ui.hudPx() follow the factors when the choice changes.
    void qmlSizesFollowTheScale()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtCraft

Item {
    width: Ui.px(100)
    height: Ui.hudPx(100)
    property real border: Ui.px(1)
}
)",
                          QUrl(u"qrc:/test_ui_scale.qml"_s));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.get());
        QVERIFY(item);
        auto *scaling = engine.singletonInstance<QcUiScaling *>("QtCraft", "UiScaling");
        QVERIFY(scaling);
        QCOMPARE(item->width(), 100.0);

        scaling->setWindowSize({1280, 720});
        scaling->setSetting(QcUiScaling::Step::Percent200);
        QCOMPARE(item->width(), 113.0);
        QCOMPARE(item->height(), 200.0);
        QCOMPARE(item->property("border").toReal(), 1.0);

        scaling->setSetting(QcUiScaling::Step::Percent75);
        QCOMPARE(item->width(), 75.0);
        QCOMPARE(item->height(), 75.0);
    }

    // The window's font grows with the screens and comes back when the scale returns to 100 %
    // (WindowFont restores the font it replaced).
    void windowFontFollowsAndComesBack()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    id: window

    WindowFont {
        target: window
    }
}
)",
                          QUrl(u"qrc:/test_window_font.qml"_s));
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *scaling = engine.singletonInstance<QcUiScaling *>("QtCraft", "UiScaling");
        QVERIFY(scaling);
        const QFont before = window->property("font").value<QFont>();

        scaling->setWindowSize({1280, 720});
        scaling->setSetting(QcUiScaling::Step::Percent200);
        const QFont scaled = window->property("font").value<QFont>();
        QCOMPARE(scaled, scaling->font());
        QVERIFY(scaled != before);

        scaling->setSetting(QcUiScaling::Step::Percent100);
        QCOMPARE(window->property("font").value<QFont>(), before);
        // A window too small for more than 100 % turns it off as well.
        scaling->setSetting(QcUiScaling::Step::Percent150);
        QVERIFY(window->property("font").value<QFont>() != before);
        scaling->setWindowSize({1024, 640});
        QCOMPARE(window->property("font").value<QFont>(), before);
    }
};

QTEST_MAIN(TestUiScale)
#include "test_ui_scale.moc"
