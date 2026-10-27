#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcBoxLegs.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace Qt::StringLiterals;

namespace {

// Vertices of one box in QcEntityMesher's meshes: six faces of four corners.
constexpr qsizetype boxVertices = 24;

const QcMobDefinition &mob(const QcContentRegistry &content, const QString &name)
{
    const std::optional<int> index = content.mobIndex(u"qtcraft:"_s + name);
    if (!index) {
        qFatal("No creature %s in the base content", qPrintable(name));
    }
    return content.mobs().at(*index);
}

// The phases of the moving parts by their place: (left, front) first.
struct FourLegs
{
    double frontLeft = 0.0;
    double frontRight = 0.0;
    double backLeft = 0.0;
    double backRight = 0.0;
};

FourLegs fourLegs(const QList<QcMobPart> &parts, const QcBoxLegs &legs)
{
    FourLegs result;
    for (qsizetype index = 0; index < legs.swings().size(); ++index) {
        const double phase = legs.swings().at(index).phase;
        const QcVec3 centre = (parts.at(index).from + parts.at(index).to) * 0.5;
        if (phase == 0.0) {
            continue;
        }
        const bool left = centre.x < 0.0;
        const bool front = centre.z < 0.0;
        (front ? (left ? result.frontLeft : result.frontRight) : (left ? result.backLeft : result.backRight)) = phase;
    }
    return result;
}

// The index of the leg on side `x` (-1 left, 1 right) and end `z` (-1 front, 1 back).
qsizetype legAt(const QList<QcMobPart> &parts, const QcBoxLegs &legs, double x, double z)
{
    for (qsizetype index = 0; index < legs.swings().size(); ++index) {
        const QcVec3 centre = (parts.at(index).from + parts.at(index).to) * 0.5;
        if (legs.swings().at(index).phase != 0.0 && centre.x * x > 0.0 && centre.z * z > 0.0) {
            return index;
        }
    }
    qFatal("No leg at %f %f", x, z);
}

qsizetype movingParts(const QcBoxLegs &legs)
{
    return std::ranges::count_if(legs.swings(), [](const QcBoxLegs::Swing &swing) { return swing.phase != 0.0; });
}

// The model has leg assemblies (decision E-895): parts turning around the vertical axis.
bool hasAssemblies(const QcBoxLegs &legs)
{
    return std::ranges::any_of(legs.swings(), [](const QcBoxLegs::Swing &swing) { return swing.axis == QcBoxLegs::Axis::Yaw; });
}

// `look` with the legs of `parts` filled in, as QcEntityFrameBuilder gives them to a walking creature.
QcEntityMesh meshOf(const QList<QcMobPart> &parts, QcEntityMesher::PartLook look)
{
    const QcBoxLegs legs = QcBoxLegs::of(parts);
    look.legs = &legs;
    QcEntityMesh mesh;
    QcEntityMesher::appendParts(mesh, parts, QcEntityMesher::Placement{}, [](const QString &) { return QRectF(0, 0, 1, 1); }, look);
    return mesh;
}

// The boxes overlap or touch.
bool touches(const QcMobPart &a, const QcMobPart &b)
{
    return a.from.x <= b.to.x && b.from.x <= a.to.x && a.from.y <= b.to.y && b.from.y <= a.to.y && a.from.z <= b.to.z
           && b.from.z <= a.to.z;
}

// `a` lies wholly inside `b`.
bool inside(const QcMobPart &a, const QcMobPart &b)
{
    return a.from.x >= b.from.x && a.to.x <= b.to.x && a.from.y >= b.from.y && a.to.y <= b.to.y && a.from.z >= b.from.z
           && a.to.z <= b.to.z;
}

// The middle of the vertices of part `index` along z.
double meanZ(const QcEntityMesh &mesh, qsizetype index, bool lowest)
{
    double sum = 0.0;
    int count = 0;
    float bottom = mesh.vertices.at(index * boxVertices).y;
    for (qsizetype v = index * boxVertices; v < (index + 1) * boxVertices; ++v) {
        bottom = std::min(bottom, mesh.vertices.at(v).y);
    }
    for (qsizetype v = index * boxVertices; v < (index + 1) * boxVertices; ++v) {
        if (!lowest || mesh.vertices.at(v).y < bottom + 0.05f) {
            sum += mesh.vertices.at(v).z;
            ++count;
        }
    }
    return sum / count;
}

} // namespace

// The walk of creatures built from static boxes (decision E-888): QcBoxLegs finds the legs in the
// parts, QcAnimator::legSwing gives the angle, QcEntityMesher::appendParts turns the legs. Also the
// eyes of the creatures built from boxes.
class TestBoxLegs : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<QcContentRegistry> m_content = QcTestUtil::loadContent();

private slots:
    void quadrupedsWalkInDiagonalPairs_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString &name : {u"wolf"_s, u"dog"_s, u"cat"_s, u"ocelot"_s, u"cow"_s, u"pig"_s, u"sheep"_s}) {
            QTest::newRow(qPrintable(name)) << name;
        }
    }

    void quadrupedsWalkInDiagonalPairs()
    {
        QFETCH(QString, name);
        const QList<QcMobPart> &parts = mob(*m_content, name).parts;
        const QcBoxLegs legs = QcBoxLegs::of(parts);

        QCOMPARE(legs.swings().size(), parts.size());
        QCOMPARE(movingParts(legs), 4);
        const FourLegs four = fourLegs(parts, legs);
        QCOMPARE(four.frontLeft, 1.0);
        QCOMPARE(four.backRight, 1.0);
        QCOMPARE(four.frontRight, -1.0);
        QCOMPARE(four.backLeft, -1.0);
        QVERIFY(legs.length() > 0.25);
        QVERIFY(legs.length() < 0.8);
    }

    // The chicken's flat feet swing with the legs they stand under, around the same hips.
    void feetFollowTheirLegs()
    {
        const QList<QcMobPart> &parts = mob(*m_content, u"chicken"_s).parts;
        const QcBoxLegs legs = QcBoxLegs::of(parts);
        QCOMPARE(movingParts(legs), 4);

        int feet = 0;
        for (qsizetype index = 0; index < parts.size(); ++index) {
            const QcMobPart &part = parts.at(index);
            if (legs.swings().at(index).phase == 0.0 || part.to.y - part.from.y > 0.1) {
                continue;
            }
            ++feet;
            const auto leg = std::ranges::find_if(legs.swings(), [&](const QcBoxLegs::Swing &swing) {
                return swing.phase == legs.swings().at(index).phase && swing.hip.y > part.to.y;
            });
            QVERIFY(leg != legs.swings().end());
            QCOMPARE(leg->hip.x, legs.swings().at(index).hip.x);
        }
        QCOMPARE(feet, 2);
        QCOMPARE(fourLegs(parts, legs).frontLeft, -fourLegs(parts, legs).frontRight);
    }

    void creaturesWithoutLegsStayStill_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString &name : {u"cod"_s, u"salmon"_s, u"slime"_s, u"silverfish"_s, u"kjut"_s, u"kjut_child"_s, u"firefly"_s}) {
            QTest::newRow(qPrintable(name)) << name;
        }
    }

    void creaturesWithoutLegsStayStill()
    {
        QFETCH(QString, name);
        QVERIFY(QcBoxLegs::of(mob(*m_content, name).parts).swings().isEmpty());
    }

    // Made-up models: what counts as a leg and a foot, and swings that stay finite.
    void syntheticModels()
    {
        const auto box = [](const QcVec3 &from, const QcVec3 &to) { return QcMobPart{from, to, u"x"_s}; };
        const QcMobPart body = box({-0.3, 0.4, -0.5}, {0.3, 0.8, 0.5});

        // A single leg is no walk.
        QVERIFY(QcBoxLegs::of({body, box({-0.2, 0.0, -0.4}, {-0.1, 0.4, -0.3})}).swings().isEmpty());

        // Boxes without height on the ground are no legs.
        QVERIFY(QcBoxLegs::of({body, box({-0.2, 0.0, -0.4}, {-0.2, 0.0, -0.4}), box({0.2, 0.0, 0.4}, {0.2, 0.0, 0.4})}).swings().isEmpty());

        // Legs touching front to back cannot swing: 0 degrees, not NaN.
        const QcBoxLegs touching = QcBoxLegs::of({body, box({-0.2, 0.0, -0.2}, {-0.1, 0.4, -0.1}), box({-0.2, 0.0, -0.1}, {-0.1, 0.4, 0.0})});
        QCOMPARE(movingParts(touching), 2);
        QCOMPARE(touching.maxSwing(), 0.0);

        // A tall part on the ground touching a leg is a leg of its own or nothing, never its foot;
        // a flat one is a foot.
        const QList<QcMobPart> parts{body,
                                     box({-0.2, 0.0, -0.4}, {-0.1, 0.4, -0.3}),
                                     box({0.1, 0.0, -0.4}, {0.2, 0.4, -0.3}),
                                     box({-0.3, 0.0, -0.4}, {-0.2, 0.3, -0.1}),
                                     box({0.1, 0.0, -0.5}, {0.25, 0.05, -0.3})};
        const QcBoxLegs legs = QcBoxLegs::of(parts);
        QCOMPARE(legs.swings().at(3).phase, 0.0);
        QCOMPARE(legs.swings().at(4).phase, legs.swings().at(2).phase);
    }

    // The cache finds the legs of a model once and gives the same object back.
    void cacheKeepsTheLegsOfAModel()
    {
        const QList<QcMobPart> &parts = mob(*m_content, u"wolf"_s).parts;
        QcBoxLegsCache cache;
        const QcBoxLegs &first = cache.of(parts);
        QCOMPARE(&cache.of(parts), &first);
        QCOMPARE(movingParts(first), 4);
    }

    void legSwingFollowsDistanceAndSpeed()
    {
        constexpr double length = 0.4;
        const double quarter = 1.8 * length / 4.0;
        QCOMPARE(QcAnimator::legSwing(quarter, 0.0, length), 0.0);
        QCOMPARE(QcAnimator::legSwing(0.0, QcAnimator::fullSwingSpeed, length), 0.0);
        QVERIFY(qFuzzyCompare(QcAnimator::legSwing(quarter, QcAnimator::fullSwingSpeed / 2.0, length), QcAnimator::maxLegSwing / 2.0));
        QVERIFY(qFuzzyCompare(QcAnimator::legSwing(quarter, 4.0, length), QcAnimator::maxLegSwing));
        QVERIFY(qFuzzyCompare(QcAnimator::legSwing(3.0 * quarter, 4.0, length), -QcAnimator::maxLegSwing));
        // Short legs step more often: the same distance is a later point of a cat's stride.
        QVERIFY(qFuzzyCompare(QcAnimator::legSwing(quarter / 2.0, 4.0, length / 2.0), QcAnimator::maxLegSwing));
        QCOMPARE(QcAnimator::legSwing(quarter, 4.0, 0.0), 0.0);
    }

    // A wolf a quarter stride on: the front left foot forward (-z), the back left one back, the
    // body where it was; sitting or standing still keeps the legs straight.
    void meshedLegsSwing()
    {
        const QList<QcMobPart> &parts = mob(*m_content, u"wolf"_s).parts;
        const QcBoxLegs legs = QcBoxLegs::of(parts);
        const double quarter = 1.8 * legs.length() / 4.0;
        const QcEntityMesh rest = meshOf(parts, {});
        const QcEntityMesh walking = meshOf(parts, {.sitting = false, .colors = {}, .walked = quarter, .speed = 1.6, .legs = nullptr});
        QCOMPARE(walking.vertices.size(), parts.size() * boxVertices);

        constexpr qsizetype body = 0;
        const qsizetype frontLeft = legAt(parts, legs, -1.0, -1.0);
        const qsizetype backLeft = legAt(parts, legs, -1.0, 1.0);
        QCOMPARE(legs.swings().at(frontLeft).phase, 1.0);
        QCOMPARE(legs.swings().at(backLeft).phase, -1.0);
        QCOMPARE(meanZ(walking, body, false), meanZ(rest, body, false));
        QVERIFY(meanZ(walking, frontLeft, true) < meanZ(rest, frontLeft, true) - 0.15);
        QVERIFY(meanZ(walking, backLeft, true) > meanZ(rest, backLeft, true) + 0.15);

        const QcEntityMesh sitting = meshOf(parts, {.sitting = true, .colors = {}, .walked = quarter, .speed = 1.6, .legs = nullptr});
        QCOMPARE(meanZ(sitting, frontLeft, true), meanZ(rest, frontLeft, true));
        const QcEntityMesh still = meshOf(parts, {.sitting = false, .colors = {}, .walked = quarter, .speed = 0.0, .legs = nullptr});
        QCOMPARE(meanZ(still, frontLeft, true), meanZ(rest, frontLeft, true));
    }

    void creaturesHaveEyes_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString &name : {u"wolf"_s, u"dog"_s, u"cat"_s, u"ocelot"_s, u"pig"_s, u"cow"_s, u"sheep"_s, u"chicken"_s, u"spider"_s,
                                    u"cave_spider"_s, u"cod"_s, u"salmon"_s, u"slime"_s}) {
            QTest::newRow(qPrintable(name)) << name;
        }
    }

    // Eyes on both sides (parts whose texture names an eye), each sitting on the face of another
    // part (the head, or the eye behind a pupil) and moving with it when the creature sits.
    void creaturesHaveEyes()
    {
        QFETCH(QString, name);
        const QList<QcMobPart> &parts = mob(*m_content, name).parts;
        int left = 0;
        int right = 0;
        for (const QcMobPart &eye : parts) {
            if (!eye.texture.contains(u"eye"_s)) {
                continue;
            }
            const auto carrier = std::ranges::find_if(parts, [&eye](const QcMobPart &part) {
                return &part != &eye && touches(part, eye) && !inside(eye, part);
            });
            QVERIFY2(carrier != parts.end(), qPrintable(eye.texture));
            QCOMPARE(eye.sitOffset, carrier->sitOffset);
            QVERIFY(eye.to.x - eye.from.x < 0.15 && eye.to.y - eye.from.y < 0.15);
            (eye.from.x + eye.to.x < 0.0 ? left : right) += 1;
        }
        QVERIFY(left > 0);
        QCOMPARE(left, right);
    }

    // At the widest swing, neighbouring legs of a side keep apart: a full 32 degrees for short legs
    // far apart, less for the cow's long legs; the spider's leg assemblies turn by maxLegYaw.
    void legsSwingNoWiderThanTheirGap()
    {
        for (const QString &name : {u"wolf"_s, u"dog"_s, u"cat"_s, u"ocelot"_s, u"pig"_s, u"chicken"_s}) {
            QCOMPARE(QcBoxLegs::of(mob(*m_content, name).parts).maxSwing(), QcAnimator::maxLegSwing);
        }
        for (const QString &name : {u"cow"_s, u"sheep"_s, u"spider"_s}) {
            const QList<QcMobPart> &parts = mob(*m_content, name).parts;
            const QcBoxLegs legs = QcBoxLegs::of(parts);
            QVERIFY2(legs.maxSwing() > 0.0 && legs.maxSwing() < QcAnimator::maxLegSwing, qPrintable(name));
        }

        // The cow's front and hind left feet, swung towards each other as far as allowed, stay apart.
        const QList<QcMobPart> &cow = mob(*m_content, u"cow"_s).parts;
        const QcBoxLegs legs = QcBoxLegs::of(cow);
        const qsizetype front = legAt(cow, legs, -1.0, -1.0);
        const qsizetype hind = legAt(cow, legs, -1.0, 1.0);
        const QcVec3 frontFoot = legs.swings().at(front).apply({cow.at(front).from.x, 0.0, cow.at(front).to.z}, -legs.maxSwing());
        const QcVec3 hindFoot = legs.swings().at(hind).apply({cow.at(hind).from.x, 0.0, cow.at(hind).from.z}, -legs.maxSwing());
        QVERIFY(hindFoot.z - frontFoot.z > 0.0);
    }
    void spidersTurnTheirWholeLegs_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("spider") << u"spider"_s;
        QTest::newRow("cave_spider") << u"cave_spider"_s;
    }

    // Leg assemblies (decision E-895): each of the eight posts with the segment on its top turns
    // around the vertical axis at the segment's inner end, inside the body; the assemblies of a
    // side alternate, and the first legs of both sides move against each other.
    void spidersTurnTheirWholeLegs()
    {
        QFETCH(QString, name);
        const QList<QcMobPart> &parts = mob(*m_content, name).parts;
        const QcBoxLegs legs = QcBoxLegs::of(parts);
        QVERIFY(hasAssemblies(legs));
        QCOMPARE(legs.maxSwing(), QcBoxLegs::maxLegYaw);
        QCOMPARE(movingParts(legs), 16);

        QList<qsizetype> posts;
        for (qsizetype index = 0; index < parts.size(); ++index) {
            const QcBoxLegs::Swing &swing = legs.swings().at(index);
            if (swing.phase == 0.0) {
                continue;
            }
            QCOMPARE(swing.axis, QcBoxLegs::Axis::Yaw);
            // The joint lies on the inner end of a segment, inside the body's width.
            QVERIFY(std::abs(swing.hip.x) < std::abs(parts.at(index).to.x + parts.at(index).from.x) * 0.5);
            if (parts.at(index).from.y == 0.0) {
                posts.append(index);
            }
        }
        QCOMPARE(posts.size(), 8);

        // Each post shares its turn with exactly one segment on top of it.
        for (const qsizetype post : posts) {
            const QcBoxLegs::Swing &turn = legs.swings().at(post);
            const auto shared = std::ranges::count_if(legs.swings(), [&turn](const QcBoxLegs::Swing &swing) {
                return swing.phase == turn.phase && swing.hip.x == turn.hip.x && swing.hip.z == turn.hip.z;
            });
            QCOMPARE(static_cast<int>(shared), 2);
        }

        // A quarter stride on: the feet keep their height and move along z, the front pair against
        // each other, neighbours of one side against each other, the body where it was.
        const double quarter = 1.8 * legs.length() / 4.0;
        const QcEntityMesh rest = meshOf(parts, {});
        const QcEntityMesh walking = meshOf(parts, {.sitting = false, .colors = {}, .walked = quarter, .speed = 3.0, .legs = nullptr});
        QCOMPARE(meanZ(walking, 0, false), meanZ(rest, 0, false));
        const auto footMove = [&](qsizetype post) { return meanZ(walking, post, true) - meanZ(rest, post, true); };
        const auto sorted = [&](bool left) {
            QList<qsizetype> side;
            std::ranges::copy_if(posts, std::back_inserter(side), [&](qsizetype p) { return (parts.at(p).from.x < 0.0) == left; });
            std::ranges::sort(side, {}, [&](qsizetype p) { return parts.at(p).from.z; });
            return side;
        };
        const QList<qsizetype> left = sorted(true);
        const QList<qsizetype> right = sorted(false);
        QCOMPARE(left.size(), 4);
        // The foot of an assembly turned by maxLegYaw moves by about sin(18 degrees) of its reach.
        const double reach = std::abs(parts.at(left.first()).from.x - legs.swings().at(left.first()).hip.x);
        QVERIFY(footMove(left.first()) < -0.25 * reach);
        QVERIFY(footMove(right.first()) > 0.25 * reach);
        for (qsizetype rank = 1; rank < 4; ++rank) {
            QVERIFY(footMove(left.at(rank)) * footMove(left.at(rank - 1)) < 0.0);
            QVERIFY(footMove(right.at(rank)) * footMove(right.at(rank - 1)) < 0.0);
        }
        for (qsizetype v = left.first() * boxVertices; v < (left.first() + 1) * boxVertices; ++v) {
            QCOMPARE(walking.vertices.at(v).y, rest.vertices.at(v).y);
        }
    }

    // Creatures with legs under the body keep swinging them forward and back: no leg assemblies.
    void quadrupedsKeepTheirSwing()
    {
        for (const QString &name : {u"wolf"_s, u"dog"_s, u"cat"_s, u"ocelot"_s, u"pig"_s, u"cow"_s, u"sheep"_s, u"chicken"_s}) {
            const QcBoxLegs legs = QcBoxLegs::of(mob(*m_content, name).parts);
            QVERIFY2(!hasAssemblies(legs), qPrintable(name));
            QVERIFY2(std::ranges::none_of(legs.swings(), [](const QcBoxLegs::Swing &swing) { return swing.axis == QcBoxLegs::Axis::Yaw; }),
                     qPrintable(name));
        }
    }

    // Made-up assemblies: a segment counts only on top of the post, with the post at its outer end
    // and its inner end on the body.
    void syntheticAssemblies()
    {
        const auto box = [](const QcVec3 &from, const QcVec3 &to) { return QcMobPart{from, to, u"x"_s}; };
        const QcMobPart body = box({-0.3, 0.4, -0.5}, {0.3, 0.8, 0.5});
        const QcMobPart leftPost = box({-0.9, 0.0, -0.1}, {-0.8, 0.5, 0.0});
        const QcMobPart rightPost = box({0.8, 0.0, -0.1}, {0.9, 0.5, 0.0});
        const QcMobPart leftSegment = box({-0.9, 0.5, -0.1}, {-0.3, 0.6, 0.0});
        const QcMobPart rightSegment = box({0.3, 0.5, -0.1}, {0.9, 0.6, 0.0});

        const QcBoxLegs both = QcBoxLegs::of({body, leftPost, leftSegment, rightPost, rightSegment});
        QVERIFY(hasAssemblies(both));
        QCOMPARE(both.swings().at(2).axis, QcBoxLegs::Axis::Yaw);
        QCOMPARE(both.swings().at(1).hip.x, -0.3);
        QCOMPARE(both.swings().at(3).hip.x, 0.3);
        // The first legs of both sides move against each other: same sign of the turn.
        QCOMPARE(both.swings().at(1).phase, both.swings().at(3).phase);

        // A segment off the body (a gap) is no assembly: the posts swing forward and back alone.
        const QcMobPart loose = box({0.4, 0.5, -0.1}, {0.9, 0.6, 0.0});
        const QcBoxLegs gap = QcBoxLegs::of({body, leftPost, rightPost, loose});
        QVERIFY(!hasAssemblies(gap));
        QCOMPARE(gap.swings().at(3).phase, 0.0);

        // A post at the inner end of the segment is no assembly either.
        const QcMobPart innerPost = box({0.3, 0.0, -0.1}, {0.4, 0.4, 0.0});
        const QcMobPart outward = box({0.3, 0.4, -0.1}, {0.9, 0.5, 0.0});
        QVERIFY(!hasAssemblies(QcBoxLegs::of({body, leftPost, innerPost, outward})));

        // A bar over two posts belongs to the outer one only: the inner post swings forward and back.
        const QcMobPart middlePost = box({0.5, 0.0, -0.1}, {0.6, 0.5, 0.0});
        const QcBoxLegs bar = QcBoxLegs::of({body, leftPost, leftSegment, middlePost, rightPost, rightSegment});
        QCOMPARE(bar.swings().at(3).axis, QcBoxLegs::Axis::Pitch);
        QVERIFY(bar.swings().at(3).phase != 0.0);
        QCOMPARE(bar.swings().at(4).axis, QcBoxLegs::Axis::Yaw);
        QCOMPARE(bar.swings().at(5).hip.x, bar.swings().at(4).hip.x);
    }
};

QTEST_GUILESS_MAIN(TestBoxLegs)
#include "test_box_legs.moc"
