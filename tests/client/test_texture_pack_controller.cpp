#include "app/controllers/QcTexturePackController.h"
#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// An image pack with one texture t:red in `colour` below <data>/texturepacks/<folder>.
bool writePack(const QString &folder, int resolution, const QColor &colour)
{
    const QString directory = QDir(QcTexturePackLibrary::userDirectory()).filePath(folder);
    QDir().mkpath(directory + u"/textures/t"_s);
    QFile manifest(directory + u"/texturepack.json"_s);
    const QByteArray json = QJsonDocument(QJsonObject{{u"format"_s, 1}, {u"name"_s, folder}, {u"resolution"_s, resolution}}).toJson();
    QImage image(resolution, resolution, QImage::Format_RGBA8888);
    image.fill(colour);
    return manifest.open(QIODevice::WriteOnly) && manifest.write(json) == json.size() && image.save(directory + u"/textures/t/red.png"_s);
}

std::shared_ptr<const QcContentRegistry> content(const QString &extra = {})
{
    auto registry = std::make_shared<QcContentRegistry>();
    registry->assets().registerTexture({u"t:red"_s, QJsonObject{{u"pattern"_s, u"noise"_s}, {u"primary"_s, u"#c00000"_s}}, {}});
    if (!extra.isEmpty()) {
        registry->assets().registerTexture({extra, QJsonObject{{u"pattern"_s, u"noise"_s}, {u"primary"_s, u"#00c000"_s}}, {}});
    }
    return registry;
}

QColor centre(const QcTextureAtlas &atlas)
{
    const QImage tile = atlas.tile(u"t:red"_s);
    return tile.pixelColor(tile.width() / 2, tile.height() / 2);
}

} // namespace

// The settings' texture pack controller at model level (decision E-301): switching at run time
// and the generation counter that drops results a later change made out of date.
class TestTexturePackController : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
        QVERIFY(writePack(u"warm"_s, 32, Qt::yellow));
        QVERIFY(writePack(u"cold"_s, 64, Qt::cyan));
    }

    void switchingInAWorldHandsOverANewAtlasWithTheSameLayout()
    {
        QcTexturePackController controller([] { return QString(); });
        controller.loadSelection({u"user:warm"_s});
        QTRY_VERIFY(!controller.busy());
        QCOMPARE(controller.stack().size(), 1);
        QVERIFY(controller.packs().size() >= 2);

        const std::shared_ptr<const QcTextureAtlas> first = controller.enterWorld(content());
        QCOMPARE(centre(*first), QColor(Qt::yellow));
        QVERIFY(!controller.status().isEmpty());

        QSignalSpy built(&controller, &QcTexturePackController::atlasBuilt);
        QSignalSpy stored(&controller, &QcTexturePackController::selectionChanged);
        controller.setActive(u"user:cold"_s, true);
        QCOMPARE(stored.size(), 1);
        QTRY_COMPARE(built.size(), 1);
        const auto second = built.front().front().value<std::shared_ptr<const QcTextureAtlas>>();
        QCOMPARE(second->tileSize(), 64);
        QCOMPARE(centre(*second), QColor(Qt::cyan));
        QVERIFY(second->sameLayout(*first));
        QCOMPARE(controller.storedSelection(), (QStringList{u"user:cold"_s, u"user:warm"_s}));
    }

    // The benchmark's pack switch (decision E-332): a whole selection at once is one build (one
    // generation step, one atlas), and nothing is stored, so the player's selection survives.
    void aSelectionForTheRunIsOneBuildAndNotStored()
    {
        QcTexturePackController controller([] { return QString(); });
        controller.loadSelection({u"user:warm"_s});
        QTRY_VERIFY(!controller.busy());
        controller.enterWorld(content());

        QSignalSpy built(&controller, &QcTexturePackController::atlasBuilt);
        QSignalSpy stored(&controller, &QcTexturePackController::selectionChanged);
        const quint64 generation = controller.generation();
        controller.selectForRun({u"user:cold"_s, u"user:warm"_s});
        QCOMPARE(controller.generation(), generation + 1);
        QCOMPARE(controller.activeSelection(), (QStringList{u"user:cold"_s, u"user:warm"_s}));
        QTRY_COMPARE(built.size(), 1);
        QTest::qWait(20);
        QCOMPARE(built.size(), 1);
        QCOMPARE(built.front().front().value<std::shared_ptr<const QcTextureAtlas>>()->tileSize(), 64);
        QCOMPARE(stored.size(), 0);
        QCOMPARE(controller.storedSelection(), QStringList{u"user:warm"_s});

        // Back to the start: again one build, still nothing stored.
        controller.selectForRun({u"user:warm"_s});
        QTRY_COMPARE(built.size(), 2);
        QCOMPARE(stored.size(), 0);
        QCOMPARE(controller.storedSelection(), QStringList{u"user:warm"_s});
        // The same selection again changes nothing.
        controller.selectForRun({u"user:warm"_s});
        QCOMPARE(controller.generation(), generation + 2);
    }

    // A build that was still running when the world ended never hands out its atlas.
    void leavingTheWorldDropsARunningBuild()
    {
        QcTexturePackController controller([] { return QString(); });
        controller.loadSelection({});
        QTRY_VERIFY(!controller.busy());
        controller.enterWorld(content());

        QSignalSpy built(&controller, &QcTexturePackController::atlasBuilt);
        controller.setActive(u"user:cold"_s, true);
        controller.leaveWorld();
        QTRY_VERIFY(!controller.busy());
        QTest::qWait(20);
        QCOMPARE(built.size(), 0);
        // The selection still counts for the menu.
        QCOMPARE(controller.storedSelection(), QStringList{u"user:cold"_s});
    }

    // A world started while a build for the menu (or the last world) runs: the world gets an atlas
    // of its own content at once, and the old result is dropped instead of replacing it.
    void enteringAWorldDuringABuildDropsTheOldResult()
    {
        QcTexturePackController controller([] { return QString(); });
        controller.loadSelection({u"user:warm"_s});
        QTRY_VERIFY(!controller.busy());
        controller.enterWorld(content());

        QSignalSpy built(&controller, &QcTexturePackController::atlasBuilt);
        controller.setActive(u"user:cold"_s, true);
        const std::shared_ptr<const QcTextureAtlas> other = controller.enterWorld(content(u"t:green"_s));
        QCOMPARE(centre(*other), QColor(Qt::cyan));
        QVERIFY(other->contains(u"t:green"_s));
        QTRY_VERIFY(!controller.busy());
        QTest::qWait(20);
        QCOMPARE(built.size(), 0);
    }

    // Reload while a build runs: the running build's result (with the old files) is dropped, the
    // folders are listed again and a pack removed from disk is reported.
    void reloadDuringABuildListsThePacksAgain()
    {
        QVERIFY(writePack(u"gone"_s, 16, Qt::magenta));
        QcTexturePackController controller([] { return QString(); });
        controller.loadSelection({u"user:gone"_s, u"user:warm"_s});
        QTRY_VERIFY(!controller.busy());
        QCOMPARE(controller.stack().size(), 2);
        controller.enterWorld(content());

        QSignalSpy built(&controller, &QcTexturePackController::atlasBuilt);
        controller.move(u"user:warm"_s, -1);
        QVERIFY(QDir(QDir(QcTexturePackLibrary::userDirectory()).filePath(u"gone"_s)).removeRecursively());
        QVERIFY(writePack(u"fresh"_s, 16, Qt::green));
        controller.refresh();
        QTRY_COMPARE(built.size(), 1);
        QTRY_VERIFY(!controller.busy());
        QCOMPARE(controller.stack().size(), 1);
        QVERIFY(controller.notices().join(u'\n').contains(u"user:gone"_s));
        // The folders were listed again: the new pack is offered, the removed one is not.
        QStringList ids;
        const QVariantList packs = controller.packs();
        for (const QVariant &pack : packs) {
            ids.append(pack.toMap().value(u"id"_s).toString());
        }
        QVERIFY(ids.contains(u"user:fresh"_s));
        QVERIFY(!ids.contains(u"user:gone"_s));
        const auto atlas = built.front().front().value<std::shared_ptr<const QcTextureAtlas>>();
        QCOMPARE(centre(*atlas), QColor(Qt::yellow));
    }

    // At most 16 active packs: switching on one more drops the last and says so.
    void theSeventeenthPackDropsTheLast()
    {
        QcTexturePackController controller([] { return QString(); });
        QStringList many;
        for (int i = 0; i < 16; ++i) {
            many.append(u"user:p%1"_s.arg(i));
        }
        controller.loadSelection(many);
        QTRY_VERIFY(!controller.busy());
        controller.setActive(u"user:warm"_s, true);
        QCOMPARE(controller.storedSelection().size(), 16);
        QCOMPARE(controller.storedSelection().value(0), u"user:warm"_s);
        QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.notices().join(u'\n').contains(u"user:p15 was switched off"_s));
    }

    // QTCRAFT_TEXTURE_PACKS: used, but the stored selection stays until the player changes it.
    void anOverrideIsNotStored()
    {
        QcTexturePackController controller([] { return QString(); });
        controller.overrideSelection({u"user:cold"_s});
        controller.loadSelection({u"user:warm"_s});
        QTRY_VERIFY(!controller.busy());
        QCOMPARE(controller.stack().size(), 1);
        QCOMPARE(controller.stack().front()->id, u"user:cold"_s);
        QCOMPARE(controller.storedSelection(), QStringList{u"user:warm"_s});
        controller.setActive(u"user:cold"_s, false);
        QVERIFY(controller.storedSelection().isEmpty());
    }

private:
    QTemporaryDir m_data;
};

QTEST_MAIN(TestTexturePackController)
#include "test_texture_pack_controller.moc"
