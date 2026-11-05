// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/admin/QcAdminViews.h"
#include "app/controllers/QcHudState.h"
#include "app/controllers/QcPhotoMode.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcDebugHud.h"
#include "app/models/QcDurationText.h"
#include "app/models/QcPointsRow.h"
#include "app/session/QcSessionParts.h"
#include "app/settings/QcSettingsController.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "worlds/QcLocalServerHost.h"

#include <QLocale>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

// The HUD's rules and the texts of model data made in C++ (row 79.63): the status rows' icons
// (QcPointsRow), what shows behind a screen (QcHudState), the debug graphs' scales, the photo mode's
// hint, the world's input with the phase, durations and the "Manage server" texts. The rows with a
// session and the game modes: test_player_hud.
class TestHudRules : public QObject
{
    Q_OBJECT

private:
    QcSessionParts m_parts;
    std::unique_ptr<QcWorldLibraryController> m_library;
    std::unique_ptr<QcSettingsController> m_settings;

private slots:
    void initTestCase()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        m_library = std::make_unique<QcWorldLibraryController>();
        m_settings = std::make_unique<QcSettingsController>(m_parts, *m_library, nullptr);
    }

    // Two points per heart: a heart is half full for an odd remainder, the icons follow the
    // maximum (the last one holds fewer points when it is odd).
    void pointsFillTheirIcons()
    {
        QCOMPARE(QcPointsRow::fills(20, 20, 2), QList<double>(10, 1.0));
        QCOMPARE(QcPointsRow::fills(7, 20, 2), (QList<double>{1, 1, 1, 0.5, 0, 0, 0, 0, 0, 0}));
        QCOMPARE(QcPointsRow::fills(0, 20, 2), QList<double>(10, 0.0));
        QCOMPARE(QcPointsRow::fills(2.5, 4, 2), (QList<double>{1, 0.25}));
        QCOMPARE(QcPointsRow::fills(15, 15, 2).size(), 8);
        QCOMPARE(QcPointsRow::fills(15, 15, 2).constLast(), 0.5);
        QCOMPARE(QcPointsRow::fills(-3, 20, 2), QList<double>(10, 0.0));
        QCOMPARE(QcPointsRow::fills(30, 20, 2), QList<double>(10, 1.0));
        QVERIFY(QcPointsRow::fills(5, 0, 2).isEmpty());
        // One point per air bubble: full or empty.
        QCOMPARE(QcPointsRow::fills(3, 10, 1), (QList<double>{1, 1, 1, 0, 0, 0, 0, 0, 0, 0}));
        QCOMPARE(QcPointsRow::fills(10, 10, 1), QList<double>(10, 1.0));
    }

    // New points change only the fills of the icons that changed; a new maximum makes the icons
    // again.
    void rowTellsOnlyChangedIcons()
    {
        QcPointsRow row(2);
        QCOMPARE(row.rowCount(), 0);
        QVERIFY(!row.shown());
        QSignalSpy reset(&row, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&row, &QAbstractItemModel::dataChanged);
        QSignalSpy shown(&row, &QcPointsRow::shownChanged);

        row.setPoints(20, 20);
        QCOMPARE(reset.count(), 1);
        QCOMPARE(row.rowCount(), 10);
        QCOMPARE(row.roleNames().value(QcPointsRow::FillRole), "fill"_ba);
        QCOMPARE(row.data(row.index(0), QcPointsRow::FillRole).toDouble(), 1.0);
        QVERIFY(!row.data(row.index(0), Qt::DisplayRole).isValid());
        QVERIFY(!row.data(row.index(10), QcPointsRow::FillRole).isValid());

        row.setPoints(17, 20);
        QCOMPARE(reset.count(), 1);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(changed[0][0].toModelIndex().row(), 8);
        QCOMPARE(changed[1][0].toModelIndex().row(), 9);
        QCOMPARE(row.data(row.index(8), QcPointsRow::FillRole).toDouble(), 0.5);

        row.setPoints(17, 20);
        QCOMPARE(changed.count(), 2);
        row.setPoints(17, 30);
        QCOMPARE(reset.count(), 2);
        QCOMPARE(row.rowCount(), 15);

        row.setShown(true);
        row.setShown(true);
        QCOMPARE(shown.count(), 1);
        QVERIFY(row.shown());
    }

    // What shows behind a screen (decision E-927): the view's parts while the view is free, the
    // world hint only without any screen.
    void hudShownGivesViewAndHint()
    {
        QcHudState hud(m_parts, *m_settings);
        QVERIFY(hud.viewShown());
        QVERIFY(hud.hintShown());
        QSignalSpy shown(&hud, &QcHudState::shownChanged);
        using H = QcScreenState::HudShown;
        const QList<std::tuple<H, bool, bool>> levels{{H::AllButHint, true, false}, {H::StatusRows, false, false}, {H::Nothing, false, false}, {H::All, true, true}};
        for (const auto &[level, view, hint] : levels) {
            hud.setHudShown(level);
            QCOMPARE(hud.hudShown(), level);
            QCOMPARE(hud.viewShown(), view);
            QCOMPARE(hud.hintShown(), hint);
        }
        QCOMPARE(shown.count(), 4);
        hud.setHudShown(H::All);
        QCOMPARE(shown.count(), 4);
    }

    // The debug graphs' lines: a frame at 60 FPS and the server's tick; the tick scale follows the
    // longest tick shown.
    void debugGraphScales()
    {
        QCOMPARE(QcDebugHud::tickBudgetMs(), 50);
        QVERIFY(qAbs(QcDebugHud::frameBudgetMs() - 16.667) < 0.001);
        QCOMPARE(QcDebugHud::frameScaleMs(), 2 * QcDebugHud::frameBudgetMs());
        QCOMPARE(QcDebugHud::tickScale({}), 2.0);
        QCOMPARE(QcDebugHud::tickScale({0.5, 1.0}), 2.0);
        QCOMPARE(QcDebugHud::tickScale({3.0, 10.0, 4.0}), 15.0);
        QCOMPARE(QcDebugHud::tickScale({80.0}), 100.0);
        QCOMPARE(QcDebugHud::tickScale({-5.0}), 2.0);
    }

    // The first hiding of the HUD in a run says how to get it back, later ones do not.
    void photoHintOnlyOnce()
    {
        QcPhotoMode photo(*m_settings);
        QSignalSpy hint(&photo, &QcPhotoMode::hudHintWanted);
        photo.toggleHud();
        QVERIFY(photo.hudHidden());
        QCOMPARE(hint.count(), 1);
        photo.toggleHud();
        photo.toggleHud();
        QVERIFY(photo.hudHidden());
        QCOMPARE(hint.count(), 1);
    }

    // The world's input follows the phase; a benchmark may enable it on its own.
    void inputFollowsThePhase()
    {
        QcScreenState screens(m_parts);
        QVERIFY(!screens.inputEnabled());
        QSignalSpy freed(&screens, &QcScreenState::mouseFreed);
        screens.setPhase(QcScreenState::Phase::Loading);
        QVERIFY(!screens.inputEnabled());
        screens.setPhase(QcScreenState::Phase::Playing);
        QVERIFY(screens.inputEnabled());
        screens.setPhase(QcScreenState::Phase::Menu);
        QVERIFY(!screens.inputEnabled());
        QCOMPARE(freed.count(), 1);
        screens.setInputEnabled(true);
        QVERIFY(screens.inputEnabled());
    }

    void durations()
    {
        QCOMPARE(QcDurationText::minutesSeconds(0), u"0:00"_s);
        QCOMPARE(QcDurationText::minutesSeconds(65), u"1:05"_s);
        QCOMPARE(QcDurationText::minutesSeconds(3600), u"60:00"_s);
        QCOMPARE(QcDurationText::minutesSeconds(-4), u"0:00"_s);
        QCOMPARE(QcDurationText::daysHoursMinutes(0), u"0 h 0 min"_s);
        QCOMPARE(QcDurationText::daysHoursMinutes((3 * 60 + 12) * 60'000 + 59'999), u"3 h 12 min"_s);
        QCOMPARE(QcDurationText::daysHoursMinutes(((2 * 24 + 5) * 60 + 7) * 60'000LL), u"2 d 5 h"_s);
    }

    // The overview's texts come formatted with the status; before the first one every text is "–".
    void adminStatusTexts()
    {
        const QVariantMap empty = QcAdminViews::emptyStatus();
        QVERIFY(!empty.value(u"measured"_s).toBool());
        for (const QString &key : {u"tpsText"_s, u"tickText"_s, u"memoryText"_s, u"uptimeText"_s, u"versionText"_s, u"world"_s, u"seed"_s}) {
            QCOMPARE(empty.value(key).toString(), u"–"_s);
        }
        QVERIFY(empty.value(u"phases"_s).toList().isEmpty());

        QcMsg::AdminStatus status;
        status.sample.ticksPerSecond = 19.54f;
        status.sample.tickP99Ms = 12.26f;
        status.sample.tickMeanMs = 3.0f;
        status.sample.players = 1234;
        status.sample.memoryMb = 512.4f;
        status.sample.sentKbPerSecond = 1.26f;
        status.peakMemoryMb = 700.0f;
        status.uptimeMs = 90ULL * 60'000;
        status.skippedTicks = 3;
        status.version = u"QtCraft 1.0"_s;
        status.protocol = 84;
        status.phaseMeanMs[0] = 0.126f;
        const QVariantMap shown = QcAdminViews::status(status);
        QVERIFY(shown.value(u"measured"_s).toBool());
        QCOMPARE(shown.value(u"tpsText"_s).toString(), u"19.5"_s);
        QCOMPARE(shown.value(u"tickText"_s).toString(), u"12.3 ms (mean 3.0)"_s);
        QCOMPARE(shown.value(u"playersText"_s).toString(), u"1,234"_s);
        QCOMPARE(shown.value(u"memoryText"_s).toString(), u"512 MB"_s);
        QCOMPARE(shown.value(u"sentText"_s).toString(), u"1.3 KB/s"_s);
        QCOMPARE(shown.value(u"peakMemoryText"_s).toString(), u"700 MB"_s);
        QCOMPARE(shown.value(u"uptimeText"_s).toString(), u"1 h 30 min"_s);
        QCOMPARE(shown.value(u"skippedText"_s).toString(), u"3"_s);
        QCOMPARE(shown.value(u"versionText"_s).toString(), u"QtCraft 1.0 (protocol 84)"_s);
        QCOMPARE(shown.value(u"phases"_s).toList().first().toMap().value(u"msText"_s).toString(), u"0.13 ms"_s);
        // Every text of the measured status has its "–" before the first one.
        for (auto it = shown.cbegin(); it != shown.cend(); ++it) {
            QVERIFY2(!it.key().endsWith(u"Text"_s) || empty.contains(it.key()), qPrintable(it.key()));
        }
    }

    // A player's line under his name, and the row of the operator looking at the screen.
    void adminPlayerRows()
    {
        QList<QcMsg::AdminPlayers::Entry> entries;
        entries.append({7, u"zoe"_s, 40, {}, QcGameMode::Creative, false, false, 0, u"10.0.0.2"_s});
        entries.append({8, u"Anna"_s, -1, {}, QcGameMode::Survival, true, true, 0, {}});
        entries.append({9, u"Bert"_s, -1, {}, QcGameMode::Adventure, false, false, 0, {}});
        const QVariantList rows = QcAdminViews::players(entries, u"ZOE"_s);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toMap().value(u"details"_s).toString(), u"Survival"_s);
        QCOMPARE(rows[1].toMap().value(u"details"_s).toString(), u"Ping unknown · Adventure"_s);
        QCOMPARE(rows[2].toMap().value(u"details"_s).toString(), u"Ping 40 ms · Creative · 10.0.0.2"_s);
        QVERIFY(!rows[0].toMap().value(u"own"_s).toBool());
        QVERIFY(rows[2].toMap().value(u"own"_s).toBool());
        // Without a session nobody is the own player.
        const QVariantList anonymous = QcAdminViews::players(entries, {});
        for (const QVariant &row : anonymous) {
            QVERIFY(!row.toMap().value(u"own"_s).toBool());
        }
    }
};

QTEST_MAIN(TestHudRules)
#include "test_hud_rules.moc"
