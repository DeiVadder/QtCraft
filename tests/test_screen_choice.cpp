#include "QcScreenChoice.h"

#include <QTest>

using namespace Qt::StringLiterals;
using Screen = QcScreenChoice::Screen;

class TestScreenChoice : public QObject
{
    Q_OBJECT

private:
    // Two external monitors left of the laptop panel; the first external one is primary.
    const QList<Screen> m_desk = {
        {u"DELL P2317H"_s, {-1920, 0, 1920, 1080}, false, true},
        {u"DELL U2414H"_s, {-3840, 0, 1920, 1080}, false, false},
        {u"Built-in Retina Display"_s, {0, 0, 1512, 982}, true, false},
    };

private slots:
    void builtinPicksThePlatformsBuiltInPanel()
    {
        const auto chosen = QcScreenChoice::choose(m_desk, u"builtin"_s);
        QVERIFY(chosen);
        QCOMPARE(chosen->index, 2);
        // The platform flag wins over the name.
        QList<Screen> renamed = m_desk;
        renamed[2].name = u"Integrierte Anzeige"_s;
        QCOMPARE(QcScreenChoice::choose(renamed, u"BuiltIn"_s)->index, 2);
    }

    void builtinFallsBackToTheNameThenThePrimaryScreen()
    {
        QList<Screen> noFlag = m_desk;
        noFlag[2].builtIn = false;
        QCOMPARE(QcScreenChoice::choose(noFlag, u"builtin"_s)->index, 2);
        noFlag[2].name = u"Color LCD"_s;
        const auto primary = QcScreenChoice::choose(noFlag, u"builtin"_s);
        QCOMPARE(primary->index, 0);
        QVERIFY(primary->reason.contains(u"primary"_s));
    }

    void indexAndName()
    {
        QCOMPARE(QcScreenChoice::choose(m_desk, u"1"_s)->index, 1);
        QCOMPARE(QcScreenChoice::choose(m_desk, u"7"_s)->index, 0);
        QCOMPARE(QcScreenChoice::choose(m_desk, u"dell u2414h"_s)->index, 1);
        QCOMPARE(QcScreenChoice::choose(m_desk, u"P2317"_s)->index, 0);
        QCOMPARE(QcScreenChoice::choose(m_desk, u"Retina"_s)->index, 2);
        QCOMPARE(QcScreenChoice::choose(m_desk, u"Projector"_s)->index, 0);
    }

    void nothingRequestedLeavesTheWindowAlone()
    {
        QVERIFY(!QcScreenChoice::choose(m_desk, QString()));
        QVERIFY(!QcScreenChoice::choose(m_desk, u"  "_s));
        QVERIFY(!QcScreenChoice::choose({}, u"builtin"_s));
    }

    void placedFullyInsideTheScreen()
    {
        const QRect panel(0, 25, 1512, 957);
        const QRect placed = QcScreenChoice::placeInside(panel, QSize(1280, 720));
        QCOMPARE(placed.size(), QSize(1280, 720));
        QVERIFY(panel.contains(placed));
        QCOMPARE(placed.left(), 116);
        // Larger than the screen: shrunk to it.
        const QRect external(-1920, 0, 1920, 1055);
        const QRect big = QcScreenChoice::placeInside(external, QSize(2560, 1440));
        QCOMPARE(big, external);
    }
};

QTEST_GUILESS_MAIN(TestScreenChoice)
#include "test_screen_choice.moc"
