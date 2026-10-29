// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/realistic/QcMaterialTextures.h"
#include "clientcore/materials/QcMaterialAtlas.h"
#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/texturepacks/QcTextureStackBuild.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/core/QcProcessMemory.h"
#include "fixtures/QcTestUtil.h"

#include <QElapsedTimer>
#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

using namespace Qt::StringLiterals;

// Measures the memory of the material maps (decision E-912). QTCRAFT_MATERIAL_MEASURE=<pack id>
// (e.g. builtin:qtcraft_hd_64) runs the stages one after another and prints a MEASURE line after
// each: loading the pack, the colour atlas, the material maps, their upload (Metal offscreen where
// available, else the Null backend) and their release. Resident memory and footprint now and their
// peaks so far (freed memory mostly stays resident on macOS, so a stage's peak is read from a run
// that ends with it: QTCRAFT_MATERIAL_MEASURE_STAGE=<n> stops after stage n). Without the variable
// only the test that the stack build hands the maps on apart from the atlas runs.
class TestMaterialMemory : public QObject
{
    Q_OBJECT

    static void report(const char *stage, qint64 ms, qint64 bytes)
    {
        qInfo().noquote() << u"MEASURE stage=%1 ms=%2 bytes_mib=%3 rss_mib=%4 rss_peak_mib=%5 footprint_mib=%6 footprint_peak_mib=%7"_s
                                 .arg(QLatin1StringView(stage))
                                 .arg(ms)
                                 .arg(static_cast<double>(bytes) / QcProcessMemory::bytesPerMb, 0, 'f', 1)
                                 .arg(QcProcessMemory::currentMb(), 0, 'f', 1)
                                 .arg(QcProcessMemory::peakMb(), 0, 'f', 1)
                                 .arg(QcProcessMemory::footprintMb(), 0, 'f', 1)
                                 .arg(QcProcessMemory::peakFootprintMb(), 0, 'f', 1);
    }

    static std::unique_ptr<QRhi> createRhi()
    {
#if defined(Q_OS_DARWIN)
        QRhiMetalInitParams metal;
        if (std::unique_ptr<QRhi> rhi{QRhi::create(QRhi::Metal, &metal)}) {
            return rhi;
        }
#endif
        QRhiNullInitParams null;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Null, &null));
    }

    static void idleFrames(QRhi *rhi)
    {
        for (int frame = 0; frame < 4; ++frame) {
            QRhiCommandBuffer *commands = nullptr;
            QCOMPARE(rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
            QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
        }
    }

private slots:
    // The maps are built apart from the atlas and handed on (decision E-912): none without the
    // option or with colourOnly (the world's first atlas), and with `colour` only the maps of that
    // atlas are built, the atlas is the same.
    void theStackBuildHandsTheMapsOnApart()
    {
        QcTextureStackBuild::Request request;
        request.content = QcTestUtil::baseContent()->registry;
        QVERIFY(!QcTextureStackBuild::run(request).materials);
        request.options.materials = true;
        request.colourOnly = true;
        const QcTextureStackBuild::Result first = QcTextureStackBuild::run(request);
        QVERIFY(first.atlas);
        QVERIFY(!first.materials);

        request.colourOnly = false;
        request.colour = first.atlas;
        const QcTextureStackBuild::Result maps = QcTextureStackBuild::run(request);
        QCOMPARE(maps.atlas, first.atlas);
        QVERIFY(maps.materials);
        QCOMPARE(maps.materials->normalLevels().front().size(), first.atlas->image().size());
        QCOMPARE(maps.materials.use_count(), 1);
    }

    void measure()
    {
        const QString pack = qEnvironmentVariable("QTCRAFT_MATERIAL_MEASURE");
        if (pack.isEmpty()) {
            QSKIP("QTCRAFT_MATERIAL_MEASURE=<pack id> measures");
        }
        bool limited = false;
        const int last = qEnvironmentVariableIntValue("QTCRAFT_MATERIAL_MEASURE_STAGE", &limited);
        const int stop = limited ? last : 99;
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::baseContent()->registry;
        report("content", 0, 0);
        // The device first: its own memory is not counted to a stage.
        const std::unique_ptr<QRhi> rhi = createRhi();
        QVERIFY(rhi);
        idleFrames(rhi.get());
        report(rhi->backend() == QRhi::Metal ? "rhi_metal" : "rhi_null", 0, 0);

        QElapsedTimer timer;
        timer.start();
        QcTextureStackBuild::Request request;
        request.ids = {pack};
        request.entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots({}));
        request.options.materials = true;
        const QcTextureStackBuild::Result loaded = QcTextureStackBuild::run(request);
        QCOMPARE(loaded.stack.size(), 1);
        report("packs", timer.elapsed(), loaded.stack.front()->decodedBytes);
        if (stop <= 1) {
            return;
        }

        timer.restart();
        auto colour = std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(*content, loaded.stack));
        report("colour", timer.elapsed(), colour->bytes());
        if (stop <= 2) {
            return;
        }

        timer.restart();
        auto maps = std::make_shared<const QcMaterialAtlas>(QcMaterialAtlas::build(*colour, *content, loaded.stack));
        report("maps", timer.elapsed(), maps->bytes());
        if (stop > 3) {
            measureUpload(rhi.get(), std::move(maps));
        }
    }

private:
    // The maps handed over as the world view does (the textures hold the only reference), uploaded
    // frame by frame, then released.
    static void measureUpload(QRhi *rhi, std::shared_ptr<const QcMaterialAtlas> maps)
    {
        QElapsedTimer timer;
        timer.start();
        QcMaterialTextures textures;
        const std::weak_ptr<const QcMaterialAtlas> watched = maps;
        const quint64 serial = maps->serial();
        textures.offer(serial, std::move(maps));
        int frames = 0;
        for (; frames < 256 && !textures.hasMaps(); ++frames) {
            QRhiCommandBuffer *commands = nullptr;
            QCOMPARE(rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
            QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
            QVERIFY(textures.prepare(rhi, true, updates));
            commands->resourceUpdate(updates);
            QCOMPARE(rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
        }
        QVERIFY(textures.hasMaps());
        QVERIFY(watched.expired());
        QCOMPARE(rhi->finish(), QRhi::FrameOpSuccess);
        report(rhi->backend() == QRhi::Metal ? "upload_metal" : "upload_null", timer.elapsed(), textures.bytes());
        qInfo().noquote() << u"MEASURE upload_frames=%1"_s.arg(frames);

        // A few frames more: deferred releases of staging buffers and textures happen there.
        idleFrames(rhi);
        report("frames", 0, 0);
        textures.release();
        idleFrames(rhi);
        report("gpu_released", 0, 0);
    }
};

QTEST_GUILESS_MAIN(TestMaterialMemory)
#include "test_material_memory.moc"
