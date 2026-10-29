// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcDrawOrigins.h"
#include "clientcore/camera/QcRenderOrigin.h"

#include <QTest>

#include <cstring>
#include <memory>

// The origins of the draws (decision E-883): the entries relative to the render anchor, the anchor
// following the camera only beyond its reach, uploads of the written entries alone, and the binding
// a draw gets with and without base instances. Uploads go to a QRhi of the Null backend (no GPU),
// whose buffers keep their contents in memory: the test reads back what each upload wrote.
class TestDrawOrigins : public QObject
{
    Q_OBJECT

    std::unique_ptr<QRhi> m_rhi;

    // The bytes one frame uploads, submitted with the frame.
    quint32 upload(QcDrawOrigins &origins)
    {
        QRhiCommandBuffer *commands = nullptr;
        if (m_rhi->beginOffscreenFrame(&commands) != QRhi::FrameOpSuccess) {
            return 0;
        }
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        const quint32 bytes = origins.upload(m_rhi.get(), updates);
        commands->resourceUpdate(updates);
        m_rhi->endOffscreenFrame();
        return bytes;
    }

    // The buffer's entries as the GPU would read them.
    static QList<QVector4D> contents(const QcDrawOrigins &origins)
    {
        QRhiBuffer *buffer = origins.inputAt(0, true).first;
        if (!buffer) {
            return {};
        }
        // The Null backend hands out the buffer's memory itself.
        const char *data = buffer->beginFullDynamicBufferUpdateForCurrentFrame();
        QList<QVector4D> entries(static_cast<qsizetype>(buffer->size() / QcDrawOrigins::stride));
        std::memcpy(entries.data(), data, static_cast<std::size_t>(entries.size()) * QcDrawOrigins::stride);
        buffer->endFullDynamicBufferUpdateForCurrentFrame();
        return entries;
    }

    // Whether the buffer holds every entry in use as at() gives it.
    static bool holdsEveryEntry(const QcDrawOrigins &origins, qsizetype count)
    {
        const QList<QVector4D> entries = contents(origins);
        if (entries.size() < count) {
            return false;
        }
        for (qsizetype entry = 0; entry < count; ++entry) {
            if (entries.at(entry) != origins.at(static_cast<quint32>(entry))) {
                return false;
            }
        }
        return true;
    }

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
    }

    void entriesAreRelativeToTheAnchor()
    {
        QcDrawOrigins origins;
        const QcVec3 camera{1e8 + 0.25, 70.5, -1e8 - 0.75};
        // The anchor is the camera's block; the camera's offset from it goes to the shaders.
        QCOMPARE(origins.follow(camera), QVector3D(0.25f, 0.5f, 0.25f));
        QVERIFY(origins.anchor() == (QcVec3{1e8, 70.0, -1e8 - 1.0}));

        const quint32 entities = origins.add(QcVec3{1e8, 70.0, -1e8 - 1.0});
        const quint32 hand = origins.add(QcVec3{1e8 + 3.0, 71.0, -1e8 - 1.0});
        const QcBlockPos corner{100'000'016, 64, -100'000'016};
        const quint32 section = origins.addSection(corner);
        QCOMPARE(entities, 0u);
        QCOMPARE(hand, 1u);
        QCOMPARE(section, 2u);
        QCOMPARE(origins.size(), 3);

        // The offsets come from the difference in double: whole numbers, exact although the
        // positions are not. Minus the camera's offset, as the shaders do, they are the old offsets
        // from the camera.
        QCOMPARE(origins.at(entities), QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
        QCOMPARE(origins.at(hand), QVector4D(3.0f, 1.0f, 0.0f, 0.0f));
        QCOMPARE(origins.at(section), QVector4D(16.0f, -6.0f, -15.0f, QcRenderOrigin::sectionCell(corner)));
        QCOMPARE(origins.at(section).toVector3D() - origins.follow(camera), QVector3D(15.75f, -6.5f, -15.25f));
        QVERIFY(QcRenderOrigin::sectionCell(corner) > 0.0f);
    }

    void anchorFollowsTheCameraBeyondItsReach()
    {
        QcDrawOrigins origins;
        const QcVec3 start{1e9 + 0.5, 70.0, 0.5};
        static_cast<void>(origins.follow(start));
        const QcBlockPos corner{1'000'000'000 + 32, 64, 16};
        const quint32 section = origins.addSection(corner);
        QCOMPARE(origins.at(section).toVector3D(), QVector3D(32.0f, -6.0f, 16.0f));

        // Within the reach the anchor and the entries stay; only the camera's offset changes.
        const QcVec3 within = start + QcVec3{QcRenderOrigin::anchorReach - 1.0, 0.0, -QcRenderOrigin::anchorReach + 1.0};
        QCOMPARE(origins.follow(within), QVector3D(127.5f, 0.0f, -126.5f));
        QCOMPARE(origins.at(section).toVector3D(), QVector3D(32.0f, -6.0f, 16.0f));

        // Beyond it the anchor moves to the camera's block and every entry follows it.
        const QcVec3 beyond = start + QcVec3{QcRenderOrigin::anchorReach + 1.0, 0.0, 0.0};
        QCOMPARE(origins.follow(beyond), QVector3D(0.5f, 0.0f, 0.5f));
        QVERIFY(origins.anchor() == (QcVec3{1e9 + 129.0, 70.0, 0.0}));
        QCOMPARE(origins.at(section).toVector3D(), QVector3D(-97.0f, -6.0f, 16.0f));
        // The corner relative to the camera is the same either way.
        QCOMPARE(origins.at(section).toVector3D() - origins.follow(beyond), QVector3D(-97.5f, -6.0f, 15.5f));
    }

    void uploadsCarryOnlyWrittenEntries()
    {
        QcDrawOrigins origins;
        static_cast<void>(origins.follow(QcVec3{0.5, 70.5, 0.5}));
        for (int i = 0; i < 40; ++i) {
            static_cast<void>(origins.add(QcVec3{static_cast<double>(i), 0.0, 0.0}));
        }
        // A new buffer takes every entry, then nothing until an entry changes.
        QCOMPARE(upload(origins), 40u * QcDrawOrigins::stride);
        QVERIFY(holdsEveryEntry(origins, 40));
        QCOMPARE(contents(origins).at(7), QVector4D(7.0f, -70.0f, 0.0f, 0.0f));
        QCOMPARE(upload(origins), 0u);

        // An origin that stays where it is writes nothing; a moved one writes its entry.
        origins.move(5, QcVec3{5.0, 0.0, 0.0});
        QCOMPARE(upload(origins), 0u);
        origins.move(5, QcVec3{5.0, 1.0, 0.0});
        QCOMPARE(origins.at(5), QVector4D(5.0f, -69.0f, 0.0f, 0.0f));
        QCOMPARE(upload(origins), QcDrawOrigins::stride);
        // The written entry is at its place, its neighbours unchanged.
        QCOMPARE(contents(origins).at(5), QVector4D(5.0f, -69.0f, 0.0f, 0.0f));
        QCOMPARE(contents(origins).at(4), QVector4D(4.0f, -70.0f, 0.0f, 0.0f));
        QCOMPARE(contents(origins).at(6), QVector4D(6.0f, -70.0f, 0.0f, 0.0f));

        // Entries close together go up as one run, entries far apart each on their own.
        origins.move(10, QcVec3{});
        origins.move(13, QcVec3{});
        QCOMPARE(upload(origins), 4u * QcDrawOrigins::stride);
        origins.move(1, QcVec3{});
        origins.move(30, QcVec3{});
        QCOMPARE(upload(origins), 2u * QcDrawOrigins::stride);
        QVERIFY(holdsEveryEntry(origins, 40));
        QCOMPARE(contents(origins).at(30), QVector4D(0.0f, -70.0f, 0.0f, 0.0f));

        // A new anchor rewrites them all.
        static_cast<void>(origins.follow(QcVec3{1000.5, 70.5, 0.5}));
        QCOMPARE(upload(origins), 40u * QcDrawOrigins::stride);
        QVERIFY(holdsEveryEntry(origins, 40));
        QCOMPARE(contents(origins).at(7), QVector4D(-993.0f, -70.0f, 0.0f, 0.0f));
        QCOMPARE(upload(origins), 0u);
    }

    void bufferGrowsAndTakesEveryEntry()
    {
        QcDrawOrigins origins;
        static_cast<void>(origins.follow(QcVec3{}));
        for (int i = 0; i < 10; ++i) {
            static_cast<void>(origins.addSection({16 * i, 0, 0}));
        }
        QCOMPARE(upload(origins), 10u * QcDrawOrigins::stride);
        // Room for 1024 entries at first.
        QCOMPARE(origins.inputAt(0, true).first->size(), 1024u * QcDrawOrigins::stride);

        // More than fit: a larger buffer, into which every entry goes again, not only the new ones.
        constexpr int count = 1500;
        for (int i = 10; i < count; ++i) {
            static_cast<void>(origins.addSection({16 * i, 0, 0}));
        }
        QCOMPARE(upload(origins), static_cast<quint32>(count) * QcDrawOrigins::stride);
        QVERIFY(origins.inputAt(0, true).first->size() >= static_cast<quint32>(count) * QcDrawOrigins::stride);
        QVERIFY(holdsEveryEntry(origins, count));
        QCOMPARE(contents(origins).at(3).x(), 48.0f);
        QCOMPARE(contents(origins).at(count - 1).x(), static_cast<float>(16 * (count - 1)));
        QCOMPARE(upload(origins), 0u);
    }

    void releasedEntriesAreReused()
    {
        QcDrawOrigins origins;
        static_cast<void>(origins.follow(QcVec3{}));
        const quint32 first = origins.addSection({0, 0, 0});
        const quint32 second = origins.addSection({16, 0, 0});
        QCOMPARE(upload(origins), 2u * QcDrawOrigins::stride);

        origins.release(first);
        QCOMPARE(origins.size(), 1);
        const quint32 third = origins.addSection({32, 0, 0});
        QCOMPARE(third, first);
        QCOMPARE(origins.at(third).x(), 32.0f);
        QCOMPARE(origins.at(second).x(), 16.0f);
        QCOMPARE(upload(origins), QcDrawOrigins::stride);
        QCOMPARE(contents(origins).at(first).x(), 32.0f);
        QCOMPARE(contents(origins).at(second).x(), 16.0f);

        // A reset forgets the entries and the anchor.
        origins.reset();
        QCOMPARE(origins.size(), 0);
        QCOMPARE(origins.add(QcVec3{}), 0u);
    }

    void drawsBindTheirEntryOnlyWithoutBaseInstances()
    {
        QcDrawOrigins origins;
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(origins.add(QcVec3{static_cast<double>(i), 0.0, 0.0}), static_cast<quint32>(i));
        }
        QCOMPARE(upload(origins), 5u * QcDrawOrigins::stride);
        // With base instances every draw binds the buffer from its start and picks the entry as its
        // first instance; without them the buffer is bound at the entry.
        for (quint32 entry = 0; entry < 5; ++entry) {
            QVERIFY(origins.inputAt(entry, true).first != nullptr);
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
