// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcMovingLights.h"
#include "clientcore/session/QcClientEntity.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

// The moving lights of the realistic mode (decision E-902): which items and creatures give light
// and how much, the falloff the shaders share, which lights a frame takes, and their packing for
// the frame uniforms.
class TestMovingLights : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_base;

    [[nodiscard]] const QcContentRegistry &content() const { return *m_base; }
    [[nodiscard]] QcItemId item(const QString &name) const { return content().itemId(name).value_or(qcNoItem); }

    static QcMovingLight warm(double x, float level) { return {QcVec3{x, 0.0, 0.0}, level, QcMovingLight::Colour::Warm}; }

private slots:
    void initTestCase()
    {
        const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
        QVERIFY(base);
        m_base = base->registry;
    }

    // A held item lights like the block it places; items without light and unknown ids give none.
    void itemsGiveTheLightOfTheirBlock()
    {
        QCOMPARE(QcMovingLights::itemLevel(content(), item(u"qtcraft:torch"_s)), 14.0f);
        QCOMPARE(QcMovingLights::itemLevel(content(), item(u"qtcraft:lava_bucket"_s)), 15.0f);
        QCOMPARE(QcMovingLights::itemLevel(content(), item(u"qtcraft:diamond"_s)), 0.0f);
        QCOMPARE(QcMovingLights::itemLevel(content(), item(u"qtcraft:stone"_s)), 0.0f);
        QCOMPARE(QcMovingLights::itemLevel(content(), qcNoItem), 0.0f);
        QCOMPARE(QcMovingLights::itemLevel(content(), 1'000'000), 0.0f);
    }

    // A firefly (glow 15) lights about seven blocks, a creature without glow nothing.
    void glowingCreaturesGiveAShareOfTheirGlow()
    {
        const std::optional<int> firefly = content().mobIndex(u"qtcraft:firefly"_s);
        QVERIFY(firefly);
        QCOMPARE(QcMovingLights::creatureLevel(content().mobs()[*firefly]), 7.5f);
        const std::optional<int> cow = content().mobIndex(u"qtcraft:cow"_s);
        QVERIFY(cow);
        QCOMPARE(QcMovingLights::creatureLevel(content().mobs()[*cow]), 0.0f);
    }

    // The local hand holds light only when alive and not a spectator, 0.3 below the eyes; an empty
    // hand or an item without light gives none.
    void handLightBelowTheEyes()
    {
        QVERIFY(QcMovingLights::handInWorld(false, QcGameModes::rules(QcGameMode::Survival)));
        QVERIFY(QcMovingLights::handInWorld(false, QcGameModes::rules(QcGameMode::Creative)));
        QVERIFY(!QcMovingLights::handInWorld(true, QcGameModes::rules(QcGameMode::Survival)));
        QVERIFY(!QcMovingLights::handInWorld(false, QcGameModes::rules(QcGameMode::Spectator)));

        const QcVec3 eye{10.0, 70.0, -3.0};
        const std::optional<QcMovingLight> torch = QcMovingLights::handLight(content(), QcItemStack{item(u"qtcraft:torch"_s), 1}, eye);
        QVERIFY(torch);
        QCOMPARE(torch->position, (eye - QcVec3{0.0, QcMovingLights::heldBelowEyes, 0.0}));
        QCOMPARE(torch->level, 14.0f);
        QCOMPARE(torch->colour, QcMovingLight::Colour::Warm);
        QVERIFY(!QcMovingLights::handLight(content(), QcItemStack{}, eye));
        QVERIFY(!QcMovingLights::handLight(content(), QcItemStack{item(u"qtcraft:diamond"_s), 1}, eye));
    }

    // Other players hold light below their eyes, creatures at 60 % of their height and glow from
    // half of it, items on the ground shine from their centre; positions between the last two
    // ticks.
    void entitiesBringTheirLights()
    {
        const QcItemId torch = item(u"qtcraft:torch"_s);
        const std::optional<int> firefly = content().mobIndex(u"qtcraft:firefly"_s);
        const std::optional<int> zombie = content().mobIndex(u"qtcraft:zombie"_s);
        QVERIFY(firefly && zombie);
        const double fireflyHeight = content().mobs()[*firefly].height;
        const double zombieHeight = content().mobs()[*zombie].height;

        QHash<quint32, QcClientEntity> entities;
        const auto add = [&entities](QcClientEntity entity, const QcVec3 &previous, const QcVec3 &position) {
            entity.id = static_cast<quint32>(entities.size() + 1);
            entity.previousPosition = previous;
            entity.position = position;
            entities.insert(entity.id, entity);
        };
        QcClientEntity player;
        player.kind = QcEntityKind::Player;
        player.held = torch;
        add(player, {0.0, 64.0, 0.0}, {2.0, 64.0, 0.0});
        QcClientEntity carrier;
        carrier.kind = QcEntityKind::Creature;
        carrier.creatureType = *zombie;
        carrier.held = torch;
        add(carrier, {10.0, 64.0, 0.0}, {10.0, 64.0, 0.0});
        QcClientEntity glowing;
        glowing.kind = QcEntityKind::Creature;
        glowing.creatureType = *firefly;
        add(glowing, {20.0, 66.0, 0.0}, {20.0, 66.0, 0.0});
        QcClientEntity dropped;
        dropped.kind = QcEntityKind::Item;
        dropped.item = QcItemStack{torch, 3};
        add(dropped, {30.0, 64.0, 0.0}, {30.0, 64.0, 0.0});
        QcClientEntity dark;
        dark.kind = QcEntityKind::Item;
        dark.item = QcItemStack{item(u"qtcraft:diamond"_s), 1};
        add(dark, {40.0, 64.0, 0.0}, {40.0, 64.0, 0.0});

        QList<QcMovingLight> lights = QcMovingLights::entityLights(content(), entities, 0.5);
        std::ranges::sort(lights, {}, [](const QcMovingLight &light) { return light.position.x; });
        QCOMPARE(lights.size(), 4);
        QCOMPARE(lights[0].position, (QcVec3{1.0, 64.0 + (QcPhysics::playerEyeHeight - QcMovingLights::heldBelowEyes), 0.0}));
        QCOMPARE(lights[1].position, (QcVec3{10.0, 64.0 + zombieHeight * 0.6, 0.0}));
        QCOMPARE(lights[1].level, 14.0f);
        QCOMPARE(lights[2].position, (QcVec3{20.0, 66.0 + fireflyHeight * 0.5, 0.0}));
        QCOMPARE(lights[2].colour, QcMovingLight::Colour::Glow);
        QCOMPARE(lights[2].level, 7.5f);
        QCOMPARE(lights[3].position, (QcVec3{30.0, 64.0 + QcMovingLights::itemCentre, 0.0}));
        QCOMPARE(lights[3].colour, QcMovingLight::Colour::Warm);
    }

    // One level less per block, like block light; nothing beyond the level, nothing for NaN.
    void brightnessFallsByOneLevelPerBlock()
    {
        QCOMPARE(QcMovingLights::brightness(15.0f, 0.0), 1.0f);
        QCOMPARE(QcMovingLights::brightness(14.0f, 0.0), 14.0f / 15.0f);
        QCOMPARE(QcMovingLights::brightness(14.0f, 4.0), 10.0f / 15.0f);
        QCOMPARE(QcMovingLights::brightness(14.0f, 14.0), 0.0f);
        QCOMPARE(QcMovingLights::brightness(14.0f, 20.0), 0.0f);
        QCOMPARE(QcMovingLights::brightness(14.0f, std::numeric_limits<double>::quiet_NaN()), 0.0f);
        QVERIFY(QcMovingLights::brightness(6.0f, 2.5) > QcMovingLights::brightness(6.0f, 3.0));
    }

    // The lights that bring the most light to the camera first, at most `count`, far and broken
    // ones left out; the order does not depend on the order of the candidates.
    void selectionTakesTheBrightestAtTheCamera()
    {
        const QList<QcMovingLight> candidates{warm(10.0, 14.0f), warm(1.0, 6.0f), warm(3.0, 14.0f), warm(80.0, 14.0f),
                                              warm(std::numeric_limits<double>::quiet_NaN(), 14.0f), warm(2.0, 0.0f)};
        const QList<QcMovingLight> chosen = QcMovingLights::select(candidates, {}, 3);
        QCOMPARE(chosen.size(), 3);
        QCOMPARE(chosen[0], warm(3.0, 14.0f));
        QCOMPARE(chosen[1], warm(1.0, 6.0f));
        QCOMPARE(chosen[2], warm(10.0, 14.0f));

        QList<QcMovingLight> reversed(candidates.crbegin(), candidates.crend());
        QCOMPARE(QcMovingLights::select(reversed, {}, 3), chosen);
        // 80 blocks away: 66 below its level, beyond `farthest`.
        QCOMPARE(QcMovingLights::select(candidates, {}, 16).size(), 3);
        QVERIFY(QcMovingLights::select(candidates, {}, 0).isEmpty());
        QVERIFY(QcMovingLights::select(candidates, {}, -1).isEmpty());

        // Equally bright lights keep a fixed order by position.
        const QList<QcMovingLight> tie{warm(-2.0, 8.0f), warm(2.0, 8.0f)};
        const QList<QcMovingLight> tieReversed{warm(2.0, 8.0f), warm(-2.0, 8.0f)};
        QCOMPARE(QcMovingLights::select(tie, {}, 1), QcMovingLights::select(tieReversed, {}, 1));
    }

    // Relative to the camera, the level below 16 plus 16 times the colour, at most `most` and the
    // room of the frame uniforms.
    void packingIsRelativeAndKeepsTheColour()
    {
        const QcVec3 camera{1000.5, 64.0, -2000.25};
        QList<QcMovingLight> lights{{camera + QcVec3{1.0, -0.3, 2.0}, 14.0f, QcMovingLight::Colour::Warm},
                                    {camera + QcVec3{-3.0, 1.0, 0.5}, 6.0f, QcMovingLight::Colour::Glow},
                                    {camera, 20.0f, QcMovingLight::Colour::Glow}};
        const QcPackedLights packed = QcMovingLights::pack(lights, camera, 8);
        QCOMPARE(packed.count, 3);
        QCOMPARE(packed.lights[0][0], 1.0f);
        QVERIFY(std::abs(packed.lights[0][1] + 0.3f) < 1e-5f);
        QCOMPARE(packed.lights[0][2], 2.0f);
        QCOMPARE(packed.lights[0][3], 14.0f);
        QCOMPARE(packed.lights[1][3], 6.0f + 16.0f);
        // A level above 15 stays below 16, so it does not read as another colour.
        QVERIFY(packed.lights[2][3] >= 31.0f && packed.lights[2][3] < 32.0f);

        QCOMPARE(QcMovingLights::pack(lights, camera, 2).count, 2);
        QCOMPARE(QcMovingLights::pack(lights, camera, 0).count, 0);
        lights.insert(0, {QcVec3{std::numeric_limits<double>::infinity(), 0.0, 0.0}, 14.0f, QcMovingLight::Colour::Warm});
        QCOMPARE(QcMovingLights::pack(lights, camera, 8).count, 3);
        QCOMPARE(QcMovingLights::pack(lights, camera, 8).lights[0][3], 14.0f);

        const QList<QcMovingLight> many(40, warm(1.0, 10.0f));
        QCOMPARE(QcMovingLights::pack(many, {}, 100).count, QcRenderOptions::maxMovingLights);
    }

    // The classic mode takes no moving lights; the realistic one more by quality.
    void qualityDecidesHowMany()
    {
        QcRenderOptions options;
        QCOMPARE(options.movingLights(), 0);
        options.mode = QcRenderOptions::Mode::Realistic;
        options.quality = QcRenderOptions::Quality::Low;
        QCOMPARE(options.movingLights(), QcRenderOptions::lowMovingLights);
        options.quality = QcRenderOptions::Quality::Medium;
        QCOMPARE(options.movingLights(), QcRenderOptions::mediumMovingLights);
        options.quality = QcRenderOptions::Quality::High;
        QCOMPARE(options.movingLights(), QcRenderOptions::maxMovingLights);
    }
};

QTEST_GUILESS_MAIN(TestMovingLights)
#include "test_moving_lights.moc"
