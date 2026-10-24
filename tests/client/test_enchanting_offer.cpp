// The parts of the enchanting screen in QML (decision E-884): an offer is taken by a click and by
// Enter with the keyboard focus (shown by the focus outline), an unusable one by neither; a screen
// reader hears how many letters are readable and why an offer cannot be taken; the level takes the
// parchment's tones; readable letters ask for no rune picture; a badge without anything to have
// shows no spark.
#include "app/models/QcPixelArtProvider.h"
#include "clientcore/settings/QcUiPalette.h"

#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    width: 600
    height: 300
    visible: true

    Column {
        objectName: "content"
        spacing: 8

        EnchantingOffer {
            property int clicks: 0

            objectName: "usable"
            offerData: ({ levelCost: 7, price: 1, signs: [{ rune: 3, text: "a" }, { rune: -1, text: "b" }], letters: 2,
                          readableLetters: 1, clue: "", levelsLacking: false, lapisLacking: false, affordable: true })
            onClicked: clicks++
        }
        EnchantingOffer {
            property int clicks: 0

            objectName: "unusable"
            offerData: ({ levelCost: 30, price: 3, signs: [{ rune: -1, text: "a" }, { rune: -1, text: "b" }], letters: 2,
                          readableLetters: 2, clue: "ab", levelsLacking: true, lapisLacking: true, affordable: false })
            onClicked: clicks++
        }
        LevelBadge {
            objectName: "nothing"
            text: "-"
            available: false
        }
    }
}
)";

// The pixel art provider, noting which pictures QML asks for. The notes outlive the engine, since
// QML keeps the pictures in a cache of the process and asks each only once.
class CountingPixelArt : public QcPixelArtProvider
{
    Q_OBJECT

public:
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        {
            const QMutexLocker lock(&m_mutex);
            m_requested.append(id);
        }
        return QcPixelArtProvider::requestImage(id, size, requestedSize);
    }

    static QStringList requested()
    {
        const QMutexLocker lock(&m_mutex);
        return m_requested;
    }

private:
    static inline QMutex m_mutex;
    static inline QStringList m_requested;
};

} // namespace

class TestEnchantingOffer : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

    // The first item below `root` (or `root` itself) that `matches`.
    static QQuickItem *find(QQuickItem *root, const std::function<bool(QQuickItem *)> &matches)
    {
        if (matches(root)) {
            return root;
        }
        const QList<QQuickItem *> children = root->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = find(child, matches)) {
                return found;
            }
        }
        return nullptr;
    }

    QQuickItem *named(const QString &name) const
    {
        return find(m_window->contentItem(), [&name](QQuickItem *item) { return item->objectName() == name; });
    }

    // What a screen reader hears of the offer `name`.
    QString spoken(const QString &name) const
    {
        QVariant text;
        [[maybe_unused]] const bool called = QMetaObject::invokeMethod(named(name), "spoken", Q_RETURN_ARG(QVariant, text));
        return text.toString();
    }

    void click(const QString &name) const
    {
        QQuickItem *item = named(name);
        const QPointF centre = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
        QTest::mouseClick(m_window, Qt::LeftButton, {}, centre.toPoint());
    }

private slots:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }

    void init()
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        // The engine owns and deletes the provider.
        m_engine->addImageProvider(QString::fromLatin1(QcPixelArtProvider::providerId), new CountingPixelArt);
        m_engine->loadData(sceneQml);
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_window->requestActivate();
    }

    void cleanup()
    {
        m_engine.reset();
        m_window = nullptr;
    }

    void clickTakesOnlyAUsableOffer()
    {
        click(u"usable"_s);
        QTRY_COMPARE(named(u"usable"_s)->property("clicks").toInt(), 1);
        click(u"unusable"_s);
        QTest::qWait(50);
        QCOMPARE(named(u"unusable"_s)->property("clicks").toInt(), 0);
    }

    void enterTakesTheFocusedOffer()
    {
        QQuickItem *usable = named(u"usable"_s);
        usable->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(usable->property("visualFocus").toBool());
        // The focus outline shows and reaches over the neighbours.
        QCOMPARE(usable->z(), 1.0);
        const QQuickItem *outline = find(usable, [](QQuickItem *item) { return QString::fromLatin1(item->metaObject()->className()).startsWith(u"FocusOutline"_s); });
        QVERIFY(outline);
        QVERIFY(outline->isVisible());

        QTest::keyClick(m_window, Qt::Key_Return);
        QTRY_COMPARE(usable->property("clicks").toInt(), 1);
        QTest::keyClick(m_window, Qt::Key_Enter);
        QTRY_COMPARE(usable->property("clicks").toInt(), 2);
    }

    void screenReadersHearLettersAndReasons()
    {
        QCOMPARE(spoken(u"usable"_s), u"Level 7, 1 lapis, 1 of 2 letters readable"_s);
        QCOMPARE(spoken(u"unusable"_s), u"Level 30, 3 lapis: ab, not enough levels and lapis"_s);
    }

    void levelsTakeTheParchmentsTones()
    {
        const auto toneOf = [this](const QString &offer) {
            const QQuickItem *badge = find(named(offer), [](QQuickItem *item) { return item->property("onParchment").isValid(); });
            return badge ? badge->property("tone").value<QColor>() : QColor();
        };
        QCOMPARE(toneOf(u"usable"_s), QcUiPalette::color(QcUiPalette::Role::LevelOnParchment));
        QCOMPARE(toneOf(u"unusable"_s), QcUiPalette::color(QcUiPalette::Role::ErrorOnParchment));
        // A badge with nothing to have shows no spark.
        const QQuickItem *spark = named(u"nothing"_s)->childItems().constFirst();
        QVERIFY(!spark->isVisible());
    }

    void readableLettersAskForNoRune()
    {
        QTRY_VERIFY(CountingPixelArt::requested().contains(u"rune/3/ink"_s));
        const QStringList requested = CountingPixelArt::requested();
        // The unusable offer's letters are all readable: no rune of its disabled ink.
        QVERIFY2(std::ranges::none_of(requested, [](const QString &id) { return id.endsWith(u"/inkDisabled"_s); }),
                 qPrintable(requested.join(u", "_s)));
    }
};

QTEST_MAIN(TestEnchantingOffer)
#include "test_enchanting_offer.moc"
