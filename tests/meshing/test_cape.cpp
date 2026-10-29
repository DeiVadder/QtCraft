// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/figures/QcAnimator.h"
#include "clientcore/figures/QcCapeSwing.h"
#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "clientcore/textures/QcTextureEmblem.h"
#include "clientcore/textures/QcTextureSynthesizer.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentParser.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/physics/QcPhysics.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QTest>
#include <QtMath>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

// Texture rectangles that tell the textures apart: texture n of `names` gets x = n.
struct NamedUv
{
    QStringList names;

    QRectF operator()(const QString &texture)
    {
        if (!names.contains(texture)) {
            names.append(texture);
        }
        return {static_cast<double>(names.indexOf(texture)), 0.0, 0.5, 1.0};
    }
};

struct Meshed
{
    QcEntityMesh mesh;
    QStringList textures;
};

Meshed meshPlayer(const QcContentRegistry &content, const QcAppearance &appearance, const QcAnimator::BodyPose &pose, double clearance = 0.0)
{
    Meshed result;
    QcEntityMesher::ModelLook look = QcFigureLook::player(content, appearance);
    look.capeClearance = clearance;
    NamedUv names;
    const QcEntityMesher::UvLookup uv = [&names](const QString &texture) { return names(texture); };
    QcEntityMesher::appendModel(result.mesh, {look, nullptr}, pose, {{0.0, 0.0, 0.0}, 0.0, QcEntityLight(1.0f, 0.0f), false}, uv);
    result.textures = names.names;
    return result;
}

QList<QcEntityVertex> withTexture(const Meshed &meshed, const QString &texture)
{
    QList<QcEntityVertex> found;
    const auto index = static_cast<double>(meshed.textures.indexOf(texture));
    for (const QcEntityVertex &vertex : meshed.mesh.vertices) {
        if (std::abs(static_cast<double>(vertex.u) - index) < 1e-6 || std::abs(static_cast<double>(vertex.u) - index - 0.5) < 1e-6) {
            found.append(vertex);
        }
    }
    return found;
}

QcAppearance caped()
{
    QcAppearance look;
    look.hair.clear();
    look.cape = u"qtcraft:green_cape"_s;
    return look;
}

} // namespace

// Capes (decisions E-240, E-241): definitions in the content, the emblem texture, the cape on the
// figure as other players and the preview see it.
class TestCape : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
        QVERIFY(base);
        m_base = base->registry;
        QVERIFY2(base->warnings.isEmpty(), qPrintable(base->warnings.join(u'\n')));
    }

    void capesAreParsedFromAppearances()
    {
        const auto cape = QcContentParser::parseAppearance(json(R"({"name":"red","kind":"cape","shape":{"width":0.5,"length":1.2,"offset":0.1},)"
            R"("layers":{"outside":{"texture":"red_cloth","tint":"shirt"},"emblem":{"texture":"red_emblem"}}})"),
                                                            u"m"_s);
        QVERIFY2(cape, qPrintable(cape ? QString() : cape.error()));
        QCOMPARE(cape->kind, QcAppearanceDefinition::Kind::Cape);
        QCOMPARE(cape->name, u"m:red"_s);
        QCOMPARE(cape->cape.width, 0.5);
        QCOMPARE(cape->cape.length, 1.2);
        QCOMPARE(cape->cape.offset, 0.1);
        // Not given: the default thickness.
        QCOMPARE(cape->cape.thickness, QcCapeShape().thickness);
        QCOMPARE(cape->layer(u"outside"_s)->tint, QcTint::Shirt);
        QCOMPARE(cape->layer(u"emblem"_s)->texture, u"m:red_emblem"_s);

        // Without a shape the default one; an outside layer is required; sizes are bounded.
        const auto plain = QcContentParser::parseAppearance(json(R"({"name":"p","kind":"cape","layers":{"outside":{"texture":"c"}}})"), u"m"_s);
        QVERIFY(plain);
        QCOMPARE(plain->cape, QcCapeShape());
        QVERIFY(!QcContentParser::parseAppearance(json(R"({"name":"p","kind":"cape","layers":{"inside":{"texture":"c"}}})"), u"m"_s));
        QVERIFY(!QcContentParser::parseAppearance(
            json(R"({"name":"p","kind":"cape","shape":{"width":5},"layers":{"outside":{"texture":"c"}}})"), u"m"_s));
        QVERIFY(!QcContentParser::parseAppearance(
            json(R"({"name":"p","kind":"cape","shape":{"offset":-0.1},"layers":{"outside":{"texture":"c"}}})"), u"m"_s));
        QVERIFY(!QcContentParser::parseAppearance(json(R"({"name":"p","kind":"cloak","layers":{"outside":{"texture":"c"}}})"), u"m"_s));

        // Creatures wear capes only on a skeleton.
        const auto mob = QcContentParser::parseMob(json(R"({"name":"x","health":5,"width":0.6,"height":1.8,"skeleton":"humanoid",)"
            R"("outfit":"tunic","cape":"red","spawn":{"weight":0}})"),
                                                   u"m"_s);
        QVERIFY2(mob, qPrintable(mob ? QString() : mob.error()));
        QCOMPARE(mob->cape, u"m:red"_s);
        QVERIFY(!QcContentParser::parseMob(json(R"({"name":"x","health":5,"width":0.6,"height":1.8,"cape":"red",)"
            R"("model":[{"from":[0,0,0],"to":[1,1,1],"texture":"t"}],"spawn":{"weight":0}})"),
                                           u"m"_s));
    }

    void baseGameOffersTheGreenCape()
    {
        const QcContentRegistry &content = *m_base;
        const QcAppearanceDefinition *green = content.assets().appearance(u"qtcraft:green_cape"_s);
        QVERIFY(green);
        QCOMPARE(green->kind, QcAppearanceDefinition::Kind::Cape);
        QVERIFY(green->playable);
        for (const QString &slot : {u"emblem"_s, u"outside"_s, u"inside"_s, u"edge"_s}) {
            QVERIFY2(green->layer(slot) && content.assets().texture(green->layer(slot)->texture), qPrintable(slot));
        }
        QCOMPARE(content.translations().translate(green->translationKey, u"de"_s), u"Grüner Umhang"_s);

        // The showcase's caped mannequin wears it.
        const auto mannequin = std::ranges::find(content.mobs(), u"qtcraft:caped_mannequin"_s, &QcMobDefinition::name);
        QVERIFY(mannequin != content.mobs().end());
        QCOMPARE(QcFigureLook::cape(content, mannequin->cape), green);
        // Drawn like the world draws skeleton creatures: the whole figure and its cape.
        QcEntityMesher::ModelLook look;
        look.model = content.assets().model(mannequin->skeleton);
        look.outfit = content.assets().appearance(mannequin->outfit);
        look.scale = mannequin->height / QcEntityMesher::modelHeight;
        QVERIFY(look.isValid());
        QcEntityMesh bare;
        QcEntityMesher::appendModel(bare, {look, nullptr}, {}, {}, [](const QString &) { return QRectF(0, 0, 1, 1); });
        look.cape = QcFigureLook::cape(content, mannequin->cape);
        QcEntityMesh dressed;
        QcEntityMesher::appendModel(dressed, {look, nullptr}, {}, {}, [](const QString &) { return QRectF(0, 0, 1, 1); });
        QVERIFY(bare.vertices.size() > 100);
        QCOMPARE(dressed.vertices.size(), bare.vertices.size() + 40);
        // Names of no cape or of another kind of appearance are no cape.
        QVERIFY(!QcFigureLook::cape(content, QString()));
        QVERIFY(!QcFigureLook::cape(content, u"qtcraft:long_hair"_s));
        QVERIFY(!QcFigureLook::cape(content, u"nomod:cape"_s));
    }

    void capesTravelWithTheContent()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_base, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> read = QcContentSerializer::read(in);
        QVERIFY2(read, qPrintable(read ? QString() : read.error()));
        const QcAppearanceDefinition *original = m_base->assets().appearance(u"qtcraft:green_cape"_s);
        const QcAppearanceDefinition *copy = read->assets().appearance(u"qtcraft:green_cape"_s);
        QVERIFY(copy);
        QCOMPARE(copy->kind, QcAppearanceDefinition::Kind::Cape);
        QCOMPARE(copy->cape, original->cape);
        QCOMPARE(copy->layers.size(), original->layers.size());
        const auto mannequin = std::ranges::find(read->mobs(), u"qtcraft:caped_mannequin"_s, &QcMobDefinition::name);
        QVERIFY(mannequin != read->mobs().end());
        QCOMPARE(mannequin->cape, u"qtcraft:green_cape"_s);
    }

    void emblemIsPixelArtFromTheData()
    {
        // The bitmap's characters take the palette's colours; the rest is cloth in the primary
        // colour, so a mod changes the motif without code.
        QcTextureSpec spec;
        spec.pattern = QcTextureSpec::Pattern::Emblem;
        spec.primary = QColor(0x41, 0xcd, 0x52);
        spec.variation = 0.0;
        const QcResult<QcTextureSpec> read =
            QcTextureEmblem::read(json("{\"bitmap\":[\"x.y\",\"\",\"..x\"],\"palette\":{\"x\":\"#ff0000\",\"y\":\"#0000ff\"}}"), spec);
        QVERIFY(read);
        const QImage image = QcTextureSynthesizer::synthesize(*read);
        QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(2, 0), QColor(0, 0, 255));
        QCOMPARE(image.pixelColor(2, 2), QColor(255, 0, 0));
        // Cloth: the primary colour or its darker weave thread, never a palette colour.
        for (const QPoint cloth : {QPoint(1, 0), QPoint(0, 1), QPoint(15, 15), QPoint(14, 15)}) {
            const QColor color = image.pixelColor(cloth);
            QVERIFY(color.green() > 180 && color.red() < 70 && color.alpha() == 255);
        }

        // Malformed bitmaps and palettes are refused.
        QcTextureSpec bad;
        QVERIFY(!QcTextureEmblem::read(json("{\"bitmap\":[\"xxxxxxxxxxxxxxxxx\"]}"), bad));
        QVERIFY(!QcTextureEmblem::read(json("{\"palette\":{\"xy\":\"#ff0000\"}}"), bad));
        QVERIFY(!QcTextureEmblem::read(json("{\"palette\":{\"x\":\"red\"}}"), bad));
        QVERIFY(!QcContentParser::parseProceduralTexture(
            json("{\"name\":\"e\",\"pattern\":\"emblem\",\"bitmap\":[\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]}"), u"m"_s));
        QVERIFY(!QcContentParser::parseProceduralTexture(json("{\"name\":\"e\",\"pattern\":\"emblem\",\"palette\":{\"x\":5}}"), u"m"_s));
        const auto kept = QcContentParser::parseProceduralTexture(json("{\"name\":\"e\",\"pattern\":\"emblem\",\"bitmap\":[\"x\"],\"palette\":{\"x\":\"#ffffff\"}}"),
                                                                  u"m"_s);
        QVERIFY(kept);
        QVERIFY(kept->procedural.contains(u"bitmap"_s) && kept->procedural.contains(u"palette"_s));

        // The green cape's emblem: Qt green cloth with the cube's dark outline at the top.
        const QcTextureAtlas atlas = QcTextureAtlas::build(*m_base);
        QVERIFY2(atlas.problems().isEmpty(), qPrintable(atlas.problems().join(u'\n')));
        const QImage emblem = atlas.tile(u"qtcraft:cape_green_emblem"_s);
        QCOMPARE(emblem.pixelColor(7, 1), QColor(0x16, 0x4d, 0x1e));
        QCOMPARE(emblem.pixelColor(7, 3), QColor(0xf2, 0xff, 0xf2));
        QVERIFY(std::abs(emblem.pixelColor(0, 0).green() - 0xcd) < 20);
    }

    void capeHangsBehindTheTorso()
    {
        const QcContentRegistry &content = *m_base;
        QcAppearance plain = caped();
        plain.cape.clear();
        QcAnimator::BodyPose pose = QcAnimator::animateBody({}, 0.0);
        const Meshed without = meshPlayer(content, plain, pose);
        const Meshed with = meshPlayer(content, caped(), pose);
        // Two pieces with four sides each, a top and a bottom: 10 quads.
        QCOMPARE(with.mesh.vertices.size(), without.mesh.vertices.size() + 40);
        QCOMPARE(with.mesh.indices.size(), without.mesh.indices.size() + 60);

        // Hanging straight: everything behind the torso's back (z = 0.125, the figure faces -Z),
        // from the shoulders down; the emblem on the outer side, the inside towards the back.
        const QcCapeShape shape = content.assets().appearance(u"qtcraft:green_cape"_s)->cape;
        const QList<QcEntityVertex> cape = with.mesh.vertices.mid(without.mesh.vertices.size());
        for (const QcEntityVertex &vertex : cape) {
            QVERIFY(vertex.z >= 0.125 + shape.offset - 1e-5);
            QVERIFY(vertex.y <= 1.35 + 1e-5 && vertex.y >= 1.35 - shape.length - 1e-5);
            QVERIFY(std::abs(vertex.x) <= shape.width / 2.0 + 1e-5);
        }
        const QList<QcEntityVertex> emblem = withTexture(with, u"qtcraft:cape_green_emblem"_s);
        QCOMPARE(emblem.size(), 4);
        for (const QcEntityVertex &vertex : emblem) {
            QVERIFY(std::abs(vertex.z - (0.125 + shape.offset + shape.thickness)) < 1e-5);
            QVERIFY(vertex.y >= 1.35 - shape.width - 1e-5);
        }
        QCOMPARE(withTexture(with, u"qtcraft:cape_green_inside"_s).size(), 8);
        QCOMPARE(withTexture(with, u"qtcraft:cape_green_edge"_s).size(), 24);

        // Room for armour: the clearance moves the whole cape outwards.
        const Meshed clear = meshPlayer(content, caped(), pose, 0.06);
        const QList<QcEntityVertex> moved = clear.mesh.vertices.mid(without.mesh.vertices.size());
        for (qsizetype i = 0; i < cape.size(); ++i) {
            QVERIFY(std::abs(moved[i].z - cape[i].z - 0.06) < 1e-5);
            QVERIFY(std::abs(moved[i].y - cape[i].y) < 1e-5);
        }
    }

    void liftAndSwayTurnTheCape()
    {
        const QcContentRegistry &content = *m_base;
        QcAppearance plain = caped();
        plain.cape.clear();
        const qsizetype bodyVertices = meshPlayer(content, plain, {}).mesh.vertices.size();
        const auto capeOf = [&](const QcAnimator::BodyPose &pose) { return meshPlayer(content, caped(), pose).mesh.vertices.mid(bodyVertices); };
        const auto lowest = [](const QList<QcEntityVertex> &vertices) {
            return std::ranges::min(vertices, {}, &QcEntityVertex::y).y;
        };
        const auto farthest = [](const QList<QcEntityVertex> &vertices) {
            return std::ranges::max(vertices, {}, &QcEntityVertex::z).z;
        };

        QcAnimator::BodyPose hanging;
        QcAnimator::BodyPose flying;
        flying.capeLift = 90.0;
        QcAnimator::BodyPose up;
        up.capeLift = QcCapeSwing::maxLift;
        // Lifted straight back the cape reaches far behind; the lower part bends further, above
        // the hinge; at the upper limit the end rises above the shoulders.
        QVERIFY(lowest(capeOf(hanging)) < 0.45);
        QVERIFY(farthest(capeOf(flying)) > 0.9);
        QVERIFY(lowest(capeOf(flying)) > 1.2);
        QVERIFY(std::ranges::max(capeOf(up), {}, &QcEntityVertex::y).y > 1.6);

        // Swaying to the left moves the lower end to -X.
        QcAnimator::BodyPose sway;
        sway.capeSway = 20.0;
        const QList<QcEntityVertex> swung = capeOf(sway);
        double lowerX = 0.0;
        int lowerCount = 0;
        for (const QcEntityVertex &vertex : swung) {
            if (vertex.y < 0.7) {
                lowerX += vertex.x;
                ++lowerCount;
            }
        }
        QVERIFY(lowerCount > 0);
        QVERIFY2(lowerX / lowerCount < -0.15, qPrintable(QString::number(lowerX / lowerCount)));

        // Sneaking leans the body forwards; the cape stays on the back and leans with it: its top
        // moves forwards with the shoulders.
        QcEntityPose sneaking;
        sneaking.sneaking = true;
        const auto top = [](const QList<QcEntityVertex> &vertices) { return std::ranges::max(vertices, {}, &QcEntityVertex::y); };
        const QcEntityVertex leaningTop = top(capeOf(QcAnimator::animateBody(sneaking, 0.0)));
        QVERIFY(leaningTop.z < top(capeOf(hanging)).z - 0.08);
    }

    void walkingBackwardsTheCapeClearsTheLegs()
    {
        const QcContentRegistry &content = *m_base;
        QcAppearance plain = caped();
        plain.cape.clear();

        // Walking backwards at walking speed, at the moment one leg swings back the farthest, with
        // the lowest lift the flutter gives.
        QcEntityPose walking;
        walking.speed = QcPhysics::walkSpeed;
        walking.walkDistance = 0.5 / 1.6;
        QcAnimator::BodyPose pose = QcAnimator::animateBody(walking, 0.0);
        QcCapeDrive backwards;
        backwards.forwardSpeed = -QcPhysics::walkSpeed;
        pose.capeLift = QcCapeSwing::maxLift;
        for (int step = 0; step < 200; ++step) {
            pose.capeLift = std::min(pose.capeLift, QcCapeSwing::target(backwards, step * 0.05).lift);
        }

        // The inner face of the cape (towards the back) as a line from the hinge over the bend to
        // the lower end, at x = 0 (no sway).
        const Meshed meshed = meshPlayer(content, caped(), pose);
        QList<QcEntityVertex> inside = withTexture(meshed, u"qtcraft:cape_green_inside"_s);
        QCOMPARE(inside.size(), 8);
        std::ranges::sort(inside, std::ranges::greater(), &QcEntityVertex::y);
        const std::array<std::pair<double, double>, 3> capeLine{{
            {inside[0].y, inside[0].z},
            {inside[3].y, std::max(inside[2].z, inside[3].z)},
            {inside[7].y, inside[7].z},
        }};
        const auto capeAt = [&capeLine](double y) {
            const auto &[upperY, upperZ] = y >= capeLine[1].first ? capeLine[0] : capeLine[1];
            const auto &[lowerY, lowerZ] = y >= capeLine[1].first ? capeLine[1] : capeLine[2];
            return lowerZ + (upperZ - lowerZ) * (y - lowerY) / (upperY - lowerY);
        };

        // The back of the leg that swings back: through its farthest corner (the heel), slanted by
        // the leg's angle. Below the hips it stays at least 2 cm in front of the cape.
        const qsizetype bodyVertices = meshPlayer(content, plain, pose).mesh.vertices.size();
        const QList<QcEntityVertex> body = meshed.mesh.vertices.mid(0, bodyVertices);
        const QcEntityVertex heel = std::ranges::max(body, {}, &QcEntityVertex::z);
        const double legAngle = qDegreesToRadians(std::abs(pose.bone(QcBoneRole::RightLeg).pitch));
        QVERIFY(heel.y < 0.4 && heel.z > 0.4);
        const double lowest = capeLine[2].first;
        QVERIFY(lowest < 0.6);
        const double from = std::max(lowest, static_cast<double>(heel.y));
        for (int centimetre = 0; from + centimetre * 0.01 <= 0.6; ++centimetre) {
            const double y = from + centimetre * 0.01;
            const double leg = heel.z - (y - heel.y) * std::tan(legAngle);
            QVERIFY2(capeAt(y) > leg + 0.02, qPrintable(u"at %1 m: leg %2, cape %3"_s.arg(y).arg(leg).arg(capeAt(y))));
        }
    }

private:
    std::shared_ptr<const QcContentRegistry> m_base;
};

QTEST_GUILESS_MAIN(TestCape)
#include "test_cape.moc"
