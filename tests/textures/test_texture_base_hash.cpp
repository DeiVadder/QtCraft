// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The textures of the base content at 16 x 16 stay byte for byte as they are (decisions E-299,
// E-1000): the detail that enlarged textures gain must never change the game's own resolution.
//
// One hash covers every frame of every texture definition. The value is kept per platform
// ("<kernel>-<cpu>", e.g. darwin-arm64), since the patterns use floating point functions whose
// last bit a platform's library may round differently. A platform without a value skips and
// prints its hash. When a change of the 16 x 16 textures is intended (a new or changed texture
// definition or pattern), run the test and put the hash it prints into `recorded` below for each
// platform that has one (others: run there, or remove their value so they skip until recorded).

#include "clientcore/textures/QcProceduralTexture.h"
#include "common/content/blocks/QcTextureDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "fixtures/QcTestUtil.h"

#include <QCryptographicHash>
#include <QSysInfo>
#include <QTest>

#include <algorithm>
#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The recorded hash (SHA-1, hex) of the 16 x 16 textures per platform.
constexpr std::array<std::pair<const char *, const char *>, 1> recorded = {{
    {"darwin-arm64", "21852b9513e863368ab3d620f340992bc4ce909d"},
}};

QString platform()
{
    return QSysInfo::kernelType() + u'-' + QSysInfo::buildCpuArchitecture();
}

} // namespace

class TestTextureBaseHash : public QObject
{
    Q_OBJECT

private slots:
    void theTexturesAt16AreUnchanged()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        QList<QcTextureDefinition> definitions = content->registry->assets().textures();
        std::ranges::sort(definitions, {}, &QcTextureDefinition::name);
        QVERIFY(definitions.size() > 300);

        QCryptographicHash hash(QCryptographicHash::Sha1);
        for (const QcTextureDefinition &definition : std::as_const(definitions)) {
            hash.addData(definition.name.toUtf8());
            hash.addData(definition.png);
            if (!definition.png.isEmpty()) {
                continue;
            }
            const QcResult<QList<QImage>> frames = QcProceduralTexture::renderFrames(definition.procedural, 16);
            QVERIFY2(frames.has_value(), qPrintable(definition.name));
            for (const QImage &frame : *frames) {
                const QImage pixels = frame.convertToFormat(QImage::Format_RGBA8888);
                QCOMPARE(pixels.size(), QSize(16, 16));
                hash.addData(QByteArrayView(pixels.constBits(), pixels.sizeInBytes()));
            }
        }

        const QByteArray actual = hash.result().toHex();
        const auto entry = std::ranges::find_if(recorded, [](const auto &value) { return platform() == QLatin1StringView(value.first); });
        if (entry == recorded.end()) {
            QSKIP(qPrintable(u"no hash recorded for %1; this platform's hash: %2"_s.arg(platform(), QString::fromLatin1(actual))));
        }
        QVERIFY2(actual == entry->second,
                 qPrintable(u"16 x 16 textures changed on %1: hash %2 (recorded %3). If intended, record the new hash "
                            "(see the comment at the top of this file)."_s.arg(platform(), QString::fromLatin1(actual),
                                                                               QString::fromLatin1(entry->second))));
    }
};

QTEST_GUILESS_MAIN(TestTextureBaseHash)
#include "test_texture_base_hash.moc"
