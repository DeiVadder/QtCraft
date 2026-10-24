#include "app/rendering/QcDrawOrigins.h"
#include "clientcore/camera/QcRenderOrigin.h"

#include <QTest>

// The offsets of a frame's draws from the camera (decision E-883) without a GPU: the entries in the
// order of the draws, their values and the binding a draw gets with and without base instances.
class TestDrawOrigins : public QObject
{
    Q_OBJECT

private slots:
    void entriesFollowTheDrawsInOrder()
    {
        QcDrawOrigins origins;
        const QcVec3 camera{1e8 + 0.25, 70.5, -1e8 - 0.75};
        origins.begin(camera);
        const quint32 entities = origins.add(QcVec3{1e8, 70.0, -1e8 - 1.0});
        const quint32 hand = origins.add(camera);
        const QcBlockPos corner{100'000'016, 64, -100'000'016};
        const quint32 section = origins.addSection(corner);
        QCOMPARE(entities, 0u);
        QCOMPARE(hand, 1u);
        QCOMPARE(section, 2u);
        QCOMPARE(origins.size(), 3);

        // The offsets come from the difference in double: exact although the positions are not.
        QCOMPARE(origins.entries()[0], QVector4D(-0.25f, -0.5f, -0.25f, 0.0f));
        QCOMPARE(origins.entries()[1], QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
        QCOMPARE(origins.entries()[2], QVector4D(15.75f, -6.5f, -15.25f, QcRenderOrigin::sectionCell(corner)));
        QVERIFY(QcRenderOrigin::sectionCell(corner) > 0.0f);

        // A new frame starts over from entry 0 with the new camera.
        origins.begin(QcVec3{});
        QCOMPARE(origins.size(), 0);
        QCOMPARE(origins.add(QcVec3{1.0, 2.0, 3.0}), 0u);
        QCOMPARE(origins.entries()[0], QVector4D(1.0f, 2.0f, 3.0f, 0.0f));
    }

    void drawsBindTheirEntryOnlyWithoutBaseInstances()
    {
        QcDrawOrigins origins;
        origins.begin(QcVec3{});
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(origins.add(QcVec3{static_cast<double>(i), 0.0, 0.0}), static_cast<quint32>(i));
        }
        // With base instances every draw binds the buffer from its start and picks the entry as its
        // first instance; without them the buffer is bound at the entry.
        for (quint32 entry = 0; entry < 5; ++entry) {
            QCOMPARE(origins.inputAt(entry, true).second, 0u);
            QCOMPARE(origins.inputAt(entry, false).second, entry * QcDrawOrigins::stride);
        }
        QCOMPARE(QcDrawOrigins::stride, 16u);
    }

    void layoutAddsThePerInstanceBinding()
    {
        QRhiVertexInputLayout layout;
        layout.setBindings({QRhiVertexInputBinding(24)});
        layout.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0)});
        const QRhiVertexInputLayout with = QcDrawOrigins::withOrigins(layout);

        QCOMPARE(with.bindingCount(), 2);
        QCOMPARE(with.bindingAt(0)->stride(), 24u);
        QCOMPARE(with.bindingAt(QcDrawOrigins::binding)->stride(), QcDrawOrigins::stride);
        QCOMPARE(with.bindingAt(QcDrawOrigins::binding)->classification(), QRhiVertexInputBinding::PerInstance);
        QCOMPARE(with.attributeCount(), 2);
        const QRhiVertexInputAttribute *origin = with.attributeAt(1);
        QCOMPARE(origin->binding(), QcDrawOrigins::binding);
        QCOMPARE(origin->location(), QcDrawOrigins::location);
        QCOMPARE(origin->format(), QRhiVertexInputAttribute::Float4);
    }
};

QTEST_GUILESS_MAIN(TestDrawOrigins)
#include "test_draw_origins.moc"
