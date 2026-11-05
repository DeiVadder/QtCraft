// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The draw stages of a launcher's sprite (decision E-987): which stage a draw shows, the fallback
// without stage textures, the rest sprite everywhere else and the draw of other figures.

#include "clientcore/entitymeshes/QcHeldItemMesher.h"
#include "clientcore/entitymeshes/QcSpriteShapes.h"
#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/player/QcLocalAnimation.h"
#include "clientcore/session/QcClientEntities.h"
#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"

#include <QImage>
#include <QSet>
#include <QTest>

#include <cstdlib>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

// The power of a draw `share` of the way through its time, for a launcher of the default length.
double powerAt(double share)
{
    return QcLauncherDefinition{}.powerAfter(share * QcLauncherDefinition{}.drawSeconds);
}

// Texels of `image` the bow's string can only reach when it is pulled back past the rest line.
int texelsBehindTheString(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += x + y > 17 && qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
        }
    }
    return count;
}

// The first texel on the diagonal that is stone, where the arrow's point lies; -1 without one.
int arrowHead(const QImage &image)
{
    for (int at = 0; at < image.width(); ++at) {
        const QRgb texel = image.pixel(at, at);
        if (qAlpha(texel) > 0 && std::abs(qRed(texel) - qBlue(texel)) < 16 && qRed(texel) < 160) {
            return at;
        }
    }
    return -1;
}

// A figure of the server the client mirrors, holding `held`.
class Archer
{
public:
    Archer(const QcContentRegistry &content, QcItemId held)
        : m_content(content)
        , m_held(held)
    {
        QcMsg::EntitySpawn spawn;
        spawn.id = id;
        spawn.held = held;
        m_entities.spawn(spawn, nullptr);
    }

    void setDrawing(bool drawing)
    {
        QcMsg::EntityMove move;
        move.id = id;
        move.flags = drawing ? QcMsg::Drawing : 0;
        move.held = m_held;
        m_entities.moves({{move}, 0}, 0);
    }
    void tick(int ticks, double serverTicks = 1.0)
    {
        for (int i = 0; i < ticks; ++i) {
            m_entities.tick(0, serverTicks);
        }
    }
    [[nodiscard]] QcClientEntity entity() const { return m_entities.all().value(id); }
    [[nodiscard]] double draw(double alpha) const { return QcLocalAnimation::poseOf(entity(), alpha, &m_content).drawProgress; }

private:
    static constexpr quint32 id = 7;

    const QcContentRegistry &m_content;
    QcItemId m_held;
    QcClientEntities m_entities;
};

} // namespace

class TestBowStages : public QObject
{
    Q_OBJECT

    QcContentRegistry m_content;
    QcItemId m_stageless = qcNoItem;

    QcItemId bowId() const { return m_content.itemId(u"qtcraft:bow"_s).value_or(qcNoItem); }
    QcEntityMesher::ItemLook bow() const { return QcFigureLook::item(m_content, bowId()); }
    bool hasTexture(const QString &name) const { return m_content.assets().texture(name) != nullptr; }
    QcEntityMesher::ItemLook bowAt(double draw) const
    {
        return QcFigureLook::atDraw(bow(), draw, [this](const QString &name) { return hasTexture(name); });
    }
    QImage painted(const QString &name) const
    {
        const QcTextureDefinition *definition = m_content.assets().texture(name);
        if (!definition) {
            return {};
        }
        return QcProceduralTexture::render(definition->procedural, 16).value_or(QImage{});
    }

private slots:
    void initTestCase()
    {
        QVERIFY(QcBaseContent::loadInto(m_content));
        QcItemDefinition stageless;
        stageless.name = u"test:stageless_bow"_s;
        stageless.texture = u"qtcraft:stick"_s;
        stageless.launcher = QcLauncherDefinition{};
        const QcResult<QcItemId> registered = m_content.registerItem(stageless);
        QVERIFY(registered.has_value());
        m_stageless = *registered;
        QVERIFY(m_content.finish().isEmpty());
    }

    // Not drawn is the rest; a draw shows the first stage from its start and changes at 65 % and
    // 90 % of the draw time, on either side of the border.
    void stageFollowsTheDrawAtItsBorders()
    {
        QCOMPARE(QcFigureLook::drawStage(-1.0), 0);
        QCOMPARE(QcFigureLook::drawStage(-0.001), 0);
        QCOMPARE(QcFigureLook::drawStage(0.0), 1);
        QCOMPARE(QcFigureLook::drawStage(powerAt(0.64)), 1);
        QCOMPARE(QcFigureLook::drawStage(powerAt(0.66)), 2);
        QCOMPARE(QcFigureLook::drawStage(powerAt(0.89)), 2);
        QCOMPARE(QcFigureLook::drawStage(powerAt(0.91)), 3);
        QCOMPARE(QcFigureLook::drawStage(1.0), 3);
    }

    // The bow's stage textures exist and each stage shows its own.
    void theBowHasItsStages()
    {
        const QcEntityMesher::ItemLook rest = bow();
        QCOMPARE(bowAt(-1.0).sprite, rest.sprite);
        QSet<QString> shown;
        for (const double draw : {0.0, powerAt(0.7), 1.0}) {
            const QString sprite = bowAt(draw).sprite;
            QVERIFY2(sprite != rest.sprite, qPrintable(sprite));
            QVERIFY(hasTexture(sprite));
            shown.insert(sprite);
        }
        QCOMPARE(shown.size(), 3);
    }

    // The string comes back and the arrow goes with it, a texel further with every stage; the
    // resting bow has no arrow.
    void stagesPullTheStringAndTheArrowBack()
    {
        const QcEntityMesher::ItemLook rest = bow();
        QCOMPARE(arrowHead(painted(rest.sprite)), -1);
        int before = texelsBehindTheString(painted(rest.sprite));
        int headBefore = -1;
        for (int stage = 1; stage <= 3; ++stage) {
            const QImage image = painted(QcFigureLook::drawSprite(rest.sprite, stage));
            QVERIFY2(!image.isNull(), qPrintable(QString::number(stage)));
            const int behind = texelsBehindTheString(image);
            QVERIFY2(behind > before, qPrintable(u"stage %1: %2 after %3"_s.arg(stage).arg(behind).arg(before)));
            before = behind;
            const int head = arrowHead(image);
            QCOMPARE(head, headBefore + 1);
            headBefore = head;
        }
    }

    // A launcher of a mod without stage textures keeps its sprite through the whole draw, and one
    // with only some of them keeps it in the stages it lacks.
    void aMissingStageKeepsTheNormalSprite()
    {
        for (const double draw : {0.0, 0.5, 1.0}) {
            QCOMPARE(QcFigureLook::atDraw(bow(), draw, [](const QString &) { return false; }).sprite, bow().sprite);
        }

        const QString last = QcFigureLook::drawSprite(bow().sprite, 3);
        const auto onlyTheLast = [&](const QString &name) { return name == last; };
        QCOMPARE(QcFigureLook::atDraw(bow(), 0.0, onlyTheLast).sprite, bow().sprite);
        QCOMPARE(QcFigureLook::atDraw(bow(), 1.0, onlyTheLast).sprite, last);
    }

    // Icons, frames and drops take the plain look: no stage, even if asked for a drawn one, and
    // only launchers have stages at all.
    void anythingButALauncherHasNoStages()
    {
        const QcEntityMesher::ItemLook stick = QcFigureLook::item(m_content, m_content.itemId(u"qtcraft:stick"_s).value_or(qcNoItem));
        QVERIFY(!stick.launcher);
        QVERIFY(bow().launcher);
        QCOMPARE(QcFigureLook::atDraw(stick, 1.0, [](const QString &) { return true; }).sprite, stick.sprite);

        QCOMPARE(bow().sprite, m_content.item(bowId()).texture);
        QCOMPARE(QcFigureLook::held(m_content, bowId())->sprite, m_content.item(bowId()).texture);
    }

    // The bow stays in the fist from stage to stage, though the arrow moves the sprite's middle.
    void aStageKeepsTheGripOfTheRestingSprite()
    {
        const auto shapeOf = [this](const QString &name) { return std::make_shared<const QcSpriteShape>(QcSpriteShape::fromImage(painted(name))); };
        QcEntityMesher::ItemLook look = bow();
        look.shape = shapeOf(look.sprite);
        const QcVec3 resting = QcEntityMesher::spriteGrip(look, 1.0);

        look.gripShape = look.shape;
        look.shape = shapeOf(QcFigureLook::drawSprite(look.sprite, 3));
        QVERIFY(look.shape->centroid() != look.gripShape->centroid());
        QCOMPARE(QcEntityMesher::spriteGrip(look, 1.0), resting);
    }

    // The hand takes the stage the atlas has, with that stage's form and the resting sprite's grip.
    void aHandHoldsTheStageOfTheAtlas()
    {
        const auto atlas = std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(m_content));
        QcSpriteShapes shapes;
        const QString rest = bow().sprite;
        const auto restShape = shapes.shape(atlas, rest);
        QVERIFY(restShape && !restShape->isEmpty());

        const auto drawn = QcFigureLook::heldAtDraw(m_content, bowId(), 1.0, shapes, atlas);
        QVERIFY(drawn);
        QCOMPARE(drawn->sprite, QcFigureLook::drawSprite(rest, 3));
        QCOMPARE(drawn->shape, shapes.shape(atlas, drawn->sprite));
        QVERIFY(drawn->shape != restShape);
        QCOMPARE(drawn->gripShape, restShape);

        const auto resting = QcFigureLook::heldAtDraw(m_content, bowId(), -1.0, shapes, atlas);
        QCOMPARE(resting->sprite, rest);
        QCOMPARE(resting->shape, restShape);

        // A launcher of a mod whose texture has no stages in the atlas.
        const auto modded = QcFigureLook::heldAtDraw(m_content, m_stageless, 1.0, shapes, atlas);
        QCOMPARE(modded->sprite, m_content.item(m_stageless).texture);
        QCOMPARE(modded->shape, shapes.shape(atlas, modded->sprite));
        QCOMPARE(modded->gripShape, modded->shape);

        QVERIFY(!QcFigureLook::heldAtDraw(m_content, qcNoItem, 1.0, shapes, atlas));
    }

    // Others only say that they draw: the client counts the ticks since and reads the power from
    // what they hold. Ending the draw ends the count.
    void othersShowHowFarTheyDrew()
    {
        const QcLauncherDefinition &launcher = *QcFigureLook::launcher(m_content, bowId());
        Archer archer(m_content, bowId());
        QCOMPARE(archer.draw(0.0), -1.0);

        archer.setDrawing(true);
        QCOMPARE(archer.draw(0.0), 0.0);
        archer.tick(1);
        QCOMPARE(archer.draw(0.0), 0.0);
        QCOMPARE(archer.draw(0.5), launcher.powerAfter(0.5 * QcPhysics::tickDuration));

        const int fullTicks = static_cast<int>(launcher.drawSeconds / QcPhysics::tickDuration);
        const int halfway = fullTicks / 2;
        archer.tick(halfway);
        QCOMPARE(archer.draw(0.0), launcher.powerAfter(halfway * QcPhysics::tickDuration));
        QCOMPARE(QcFigureLook::drawStage(archer.draw(0.0)), 1);
        archer.tick(fullTicks);
        QCOMPARE(archer.draw(0.0), 1.0);
        QCOMPARE(QcFigureLook::drawStage(archer.draw(0.0)), 3);

        archer.setDrawing(false);
        QCOMPARE(archer.draw(0.0), -1.0);
        archer.tick(1);
        archer.setDrawing(true);
        QCOMPARE(archer.draw(0.0), 0.0);
    }

    // An archer that holds no launcher (a thrown potion) aims as before.
    void anArcherWithoutALauncherShowsFullyDrawn()
    {
        Archer archer(m_content, qcNoItem);
        archer.setDrawing(true);
        QCOMPARE(archer.draw(0.0), 1.0);

        Archer withoutContent(m_content, bowId());
        withoutContent.setDrawing(true);
        QCOMPARE(QcLocalAnimation::poseOf(withoutContent.entity(), 0.0).drawProgress, 1.0);
    }

    // The draw runs with the server: a frozen one stays where it is, within the tick too, and a
    // server at double speed draws twice as fast.
    void othersDrawAtTheSpeedOfTheServer()
    {
        const QcLauncherDefinition &launcher = *QcFigureLook::launcher(m_content, bowId());
        Archer archer(m_content, bowId());
        archer.setDrawing(true);
        archer.tick(5);
        const double frozen = archer.draw(0.0);
        QVERIFY(frozen > 0.0);
        archer.tick(100, 0.0);
        QCOMPARE(archer.draw(0.0), frozen);
        QCOMPARE(archer.draw(0.9), frozen);
        QCOMPARE(QcFigureLook::drawStage(archer.draw(0.9)), 1);

        archer.tick(3, 2.0);
        QCOMPARE(archer.draw(0.0), launcher.powerAfter(10.0 * QcPhysics::tickDuration));
        QCOMPARE(archer.draw(0.5), launcher.powerAfter(11.0 * QcPhysics::tickDuration));
    }

    // A shot and a new draw that arrive between two client ticks start the count anew.
    void aNewDrawBetweenTwoTicksStartsAgain()
    {
        Archer archer(m_content, bowId());
        archer.setDrawing(true);
        archer.tick(30);
        QCOMPARE(archer.draw(0.0), 1.0);

        archer.setDrawing(false);
        archer.setDrawing(true);
        QCOMPARE(archer.draw(0.0), 0.0);
        QCOMPARE(QcFigureLook::drawStage(archer.draw(0.0)), 1);
    }

    // The look of no item, an unknown item or one that shoots nothing has no launcher.
    void onlyLaunchersHaveALauncher()
    {
        QVERIFY(QcFigureLook::launcher(m_content, bowId()));
        QVERIFY(!QcFigureLook::launcher(m_content, qcNoItem));
        QVERIFY(!QcFigureLook::launcher(m_content, m_content.itemId(u"qtcraft:stick"_s).value_or(qcNoItem)));
    }
};

QTEST_GUILESS_MAIN(TestBowStages)
#include "test_bow_stages.moc"
