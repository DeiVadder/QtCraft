// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// A failed start of the main window (decision E-964): only a missing part of Qt in a plain start
// starts the program once more, and never a third time.

#include "app/debug/QcStartCheck.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// The line the playtest's first start on Windows wrote (2026-11-02).
const QString playtestError = u"qrc:/qt/qml/QtCraft/Main.qml:4:1: module \"QtQuick.Templates\" plugin \"qtquicktemplates2plugin\" not found"_s;

const QStringList plain{u"C:/Games/QtCraft/qtcraft.exe"_s};

} // namespace

class TestStartCheck : public QObject
{
    Q_OBJECT

private slots:
    void aMissingQtPluginIsAMissingPart()
    {
        QVERIFY(QcStartCheck::missingQtPart({u"some other line"_s, playtestError}));
        QVERIFY(QcStartCheck::missingQtPart(
            {u"qrc:/qt/qml/QtCraft/Main.qml:4:1: Cannot load library C:/QtCraft/qml/QtQuick/qquick2plugin.dll: access denied"_s}));
    }

    void anErrorInTheGamesQmlIsNot()
    {
        QVERIFY(!QcStartCheck::missingQtPart({u"qrc:/qt/qml/QtCraft/Main.qml:12:5: Expected token `}'"_s}));
        QVERIFY(!QcStartCheck::missingQtPart({u"qrc:/qt/qml/QtCraft/Main.qml:20:9: Type HudLayer unavailable"_s}));
        QVERIFY(!QcStartCheck::missingQtPart({}));
    }

    void aPlainStartMayRestartOnce()
    {
        const QProcessEnvironment empty;
        QVERIFY(QcStartCheck::mayRestart(false, plain, empty));

        QProcessEnvironment second;
        second.insert(QString::fromLatin1(QcStartCheck::retryVariable), u"1"_s);
        QVERIFY(QcStartCheck::plainStart(false, plain, second));
        QVERIFY(!QcStartCheck::mayRestart(false, plain, second));
    }

    void scriptedStartsNeverRestart()
    {
        const QProcessEnvironment empty;
        QVERIFY(!QcStartCheck::mayRestart(false, plain + QStringList{u"--play"_s}, empty));
        QVERIFY(!QcStartCheck::mayRestart(true, plain, empty));

        QProcessEnvironment screenshot;
        screenshot.insert(u"QTCRAFT_SCREENSHOT"_s, u"shot.png"_s);
        QVERIFY(!QcStartCheck::mayRestart(false, plain, screenshot));
    }
};

QTEST_GUILESS_MAIN(TestStartCheck)
#include "test_start_check.moc"
