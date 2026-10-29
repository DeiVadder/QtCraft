// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcTetherMesher.h"
#include "clientcore/meshing/QcTethers.h"
#include "clientcore/session/QcClientEntity.h"
#include "common/content/registry/QcContentSerializer.h"
#include "QcTestUtil.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The threads between Kjuts and their children on the client (decision E-839): each little Kjut is
// tied to the nearest Kjut within the family's tether range, from the middle of one body to the
// middle of the other, and keeps that parent from frame to frame unless another comes clearly
// closer; a parent that died (left the living entities) leaves no thread, a new content is learnt
// anew, and a thread is drawn as two crossed ribbons seen from both sides.
class TestKjutThreads : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QHash<quint32, QcClientEntity> m_entities;
    QcTethers m_tethers;

    int kind(const QString &name) const { return *m_content->mobIndex(name); }

    // A creature of `type` that stood at `from` last tick and stands at `to` now.
    void place(quint32 id, int type, const QcVec3 &from, const QcVec3 &to)
    {
        QcClientEntity entity;
        entity.id = id;
        entity.kind = QcEntityKind::Creature;
        entity.creatureType = type;
        entity.previousPosition = from;
        entity.position = to;
        m_entities.insert(id, entity);
    }

    double middle(const QString &name) const { return m_content->mobs()[kind(name)].height * 0.5; }

private slots:
    void init()
    {
        m_entities.clear();
        m_tethers = {};
    }

    void theFamilyHasAThread()
    {
        const QcMobDefinition &parent = m_content->mobs()[kind(u"qtcraft:kjut"_s)];
        QVERIFY(parent.family);
        QCOMPARE(parent.family->tetherTexture, u"qtcraft:kjut_thread"_s);
        QVERIFY(parent.family->tetherRange >= 8.0);
    }

    // Each child to the nearest parent within range, from the middle of one body to the other,
    // between the last two ticks; a child out of range has none.
    void childrenAreTiedToTheNearestParent()
    {
        const int kjut = kind(u"qtcraft:kjut"_s);
        const int little = kind(u"qtcraft:kjut_child"_s);
        place(1, kjut, {0.0, 64.0, 0.0}, {0.0, 64.0, 0.0});
        place(2, kjut, {6.0, 64.0, 0.0}, {6.0, 64.0, 0.0});
        place(3, little, {1.0, 64.0, 0.0}, {1.2, 64.0, 0.0});
        place(4, little, {5.0, 64.0, 0.0}, {5.0, 64.0, 0.0});
        place(5, little, {40.0, 64.0, 0.0}, {40.0, 64.0, 0.0});

        const QList<QcTetherMesher::Thread> threads = m_tethers.threads(m_entities, m_content, 0.5);
        QCOMPARE(threads.size(), 2);
        const auto toward = [&threads](double x) {
            return std::ranges::find_if(threads, [x](const QcTetherMesher::Thread &thread) { return std::abs(thread.to.x - x) < 1e-9; });
        };
        const auto first = toward(1.1);
        QVERIFY(first != threads.end());
        QCOMPARE(first->from.x, 0.0);
        QCOMPARE(first->from.y, 64.0 + middle(u"qtcraft:kjut"_s));
        QCOMPARE(first->to.y, 64.0 + middle(u"qtcraft:kjut_child"_s));
        QCOMPARE(first->texture, u"qtcraft:kjut_thread"_s);
        const auto second = toward(5.0);
        QVERIFY(second != threads.end());
        QCOMPARE(second->from.x, 6.0);
    }

    // The client gets the content from the server: the threads travel with it (protocol 66), the
    // rest of the family does not.
    void theThreadTravelsWithTheContent()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        QcResult<QcContentRegistry> client = QcContentSerializer::read(in);
        QVERIFY(client.has_value());
        const QcMobDefinition &parent = client->mobs()[kind(u"qtcraft:kjut"_s)];
        QVERIFY(parent.family);
        QCOMPARE(parent.family->childType, kind(u"qtcraft:kjut_child"_s));
        QCOMPARE(parent.family->tetherTexture, u"qtcraft:kjut_thread"_s);
        QCOMPARE(parent.family->tetherRange, m_content->mobs()[kind(u"qtcraft:kjut"_s)].family->tetherRange);
        QVERIFY(parent.family->popDrops.isEmpty());

        place(1, kind(u"qtcraft:kjut"_s), {0.0, 64.0, 0.0}, {0.0, 64.0, 0.0});
        place(2, kind(u"qtcraft:kjut_child"_s), {1.0, 64.0, 0.0}, {1.0, 64.0, 0.0});
        QCOMPARE(m_tethers.threads(m_entities, std::make_shared<const QcContentRegistry>(std::move(*client)), 1.0).size(), 1);
    }

    // A child keeps its parent while another is only a little nearer; one clearly nearer takes it
    // over, and so does the nearer one when the old parent leaves the range.
    void aChildKeepsItsParentUntilAnotherIsClearlyNearer()
    {
        const int kjut = kind(u"qtcraft:kjut"_s);
        place(1, kjut, {0.0, 64.0, 0.0}, {0.0, 64.0, 0.0});
        place(2, kjut, {4.0, 64.0, 0.0}, {4.0, 64.0, 0.0});
        place(3, kind(u"qtcraft:kjut_child"_s), {1.5, 64.0, 0.0}, {1.5, 64.0, 0.0});
        const auto parentX = [this] {
            const QList<QcTetherMesher::Thread> threads = m_tethers.threads(m_entities, m_content, 1.0);
            return threads.size() == 1 ? threads.constFirst().from.x : -1.0;
        };
        QCOMPARE(parentX(), 0.0);

        // 2.1 m from the first, 1.9 m from the second: it stays with the first.
        m_entities[3].position = {2.1, 64.0, 0.0};
        QCOMPARE(parentX(), 0.0);
        // 3.0 m from the first, 1.0 m from the second: the second takes it.
        m_entities[3].position = {3.0, 64.0, 0.0};
        QCOMPARE(parentX(), 4.0);
        // Back to the middle: it now stays with the second.
        m_entities[3].position = {1.9, 64.0, 0.0};
        QCOMPARE(parentX(), 4.0);
        // The second leaves the range: the first takes it again.
        m_entities[2].position = {40.0, 64.0, 0.0};
        QCOMPARE(parentX(), 0.0);
    }

    // A dead parent is no longer among the living entities: its threads snap.
    void aDeadParentLeavesNoThread()
    {
        place(1, kind(u"qtcraft:kjut"_s), {0.0, 64.0, 0.0}, {0.0, 64.0, 0.0});
        place(2, kind(u"qtcraft:kjut_child"_s), {1.0, 64.0, 0.0}, {1.0, 64.0, 0.0});
        QCOMPARE(m_tethers.threads(m_entities, m_content, 1.0).size(), 1);
        m_entities.remove(1);
        QVERIFY(m_tethers.threads(m_entities, m_content, 1.0).isEmpty());
        // Other creatures have no threads.
        place(3, kind(u"qtcraft:slime"_s), {0.5, 64.0, 0.0}, {0.5, 64.0, 0.0});
        place(4, kind(u"qtcraft:zombie"_s), {0.5, 64.0, 1.0}, {0.5, 64.0, 1.0});
        QVERIFY(m_tethers.threads(m_entities, m_content, 1.0).isEmpty());
    }

    // Two ribbons across the thread, each seen from both sides, along its whole length and no
    // wider than the thread.
    void aThreadIsTwoCrossedRibbons()
    {
        QcEntityMesh mesh;
        const QcTetherMesher::Thread thread{{0.0, 64.0, 0.0}, {1.0, 65.0, 0.5}, u"qtcraft:kjut_thread"_s};
        QcTetherMesher::append(mesh, thread, QRectF(0.1, 0.2, 0.05, 0.05), QcEntityLight(1.0f, 1.0f));
        QCOMPARE(mesh.vertices.size(), 8);
        QCOMPARE(mesh.indices.size(), 24);
        for (const QcEntityVertex &vertex : std::as_const(mesh.vertices)) {
            const QcVec3 at{vertex.x, vertex.y, vertex.z};
            const double toStart = (at - thread.from).length();
            const double toEnd = (at - thread.to).length();
            QVERIFY(std::min(toStart, toEnd) <= QcTetherMesher::thickness);
            QCOMPARE(vertex.blockLight, 1.0f);
        }
        // A thread of no length is not drawn.
        QcEntityMesh none;
        QcTetherMesher::append(none, {thread.from, thread.from, thread.texture}, QRectF(), QcEntityLight());
        QVERIFY(none.vertices.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestKjutThreads)
#include "test_kjut_threads.moc"
