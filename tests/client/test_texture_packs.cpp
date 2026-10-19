#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/texturepacks/QcTexturePackLoader.h"
#include "clientcore/texturepacks/QcTextureSources.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QByteArray png(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QImage filled(int width, int height, const QColor &colour)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(colour);
    return image;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray manifestBytes(int resolution, const QJsonObject &extra = {})
{
    QJsonObject manifest{{u"format"_s, 1}, {u"name"_s, u"Test pack"_s}, {u"resolution"_s, resolution}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        manifest.insert(it.key(), it.value());
    }
    return QJsonDocument(manifest).toJson();
}

QString messages(const QcTexturePackProblems &problems)
{
    QStringList lines;
    for (const QcTexturePackProblem &problem : problems) {
        lines.append(problem.englishText());
    }
    return lines.join(u'\n');
}

bool hasProblem(const QcTexturePack &pack, const QString &file, const QString &part)
{
    return std::ranges::any_of(pack.problems, [&](const QcTexturePackProblem &problem) {
        return problem.file == file && problem.englishText().contains(part);
    });
}

QcTexturePack loadOk(const QString &directory, const QcTexturePackLoader::Limits &limits = {})
{
    std::expected<QcTexturePack, QcTexturePackProblem> pack = QcTexturePackLoader::load(directory, u"user:test"_s, QcTexturePack::Origin::User, limits);
    if (!pack) {
        qWarning() << pack.error().englishText();
        return {};
    }
    return *pack;
}

QJsonObject procedural(const char *pattern, const char *primary)
{
    return {{u"pattern"_s, QString::fromLatin1(pattern)}, {u"primary"_s, QString::fromLatin1(primary)}, {u"seed"_s, 3}};
}

quint32 crc32(const QByteArray &bytes)
{
    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

QByteArray chunk(const QByteArray &kind, const QByteArray &body)
{
    QByteArray bytes;
    const auto put = [&bytes](quint32 value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            bytes.append(char((value >> shift) & 0xFF));
        }
    };
    put(quint32(body.size()));
    bytes.append(kind + body);
    put(crc32(kind + body));
    return bytes;
}

// A tiny PNG whose header claims width x height pixels: a decompression bomb if anything decoded it.
QByteArray headerBomb(quint32 width, quint32 height)
{
    QByteArray header;
    for (const quint32 value : {width, height}) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            header.append(char((value >> shift) & 0xFF));
        }
    }
    header.append(QByteArray("\x08\x06\x00\x00\x00", 5));
    return QByteArray("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", header) + chunk("IDAT", QByteArray(16, '\0')) + chunk("IEND", {});
}

} // namespace

class TestTexturePacks : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Never the player's own data folder.
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    void resolutionsArePowersOfTwoFrom16To512()
    {
        for (const int good : {16, 32, 64, 128, 256, 512}) {
            QVERIFY2(QcTexturePackManifest::isValidResolution(good), qPrintable(QString::number(good)));
        }
        for (const int bad : {0, 8, 15, 24, 48, 96, 1024, -16}) {
            QVERIFY2(!QcTexturePackManifest::isValidResolution(bad), qPrintable(QString::number(bad)));
        }
    }

    void manifestReadsEveryField()
    {
        const QJsonObject extra{
            {u"description"_s, u"  A pack. "_s},
            {u"license"_s, u"CC-BY-4.0"_s},
            {u"procedural"_s, true},
            {u"preview"_s, u"preview.png"_s},
            {u"credits"_s, QJsonArray{QJsonObject{{u"author"_s, u"Ada"_s}, {u"license"_s, u"CC0-1.0"_s}, {u"source"_s, u"own work"_s}}}},
        };
        const auto manifest = QcTexturePackManifest::parse(manifestBytes(64, extra));
        QVERIFY(manifest.has_value());
        QCOMPARE(manifest->name, u"Test pack"_s);
        QCOMPARE(manifest->description, u"A pack."_s);
        QCOMPARE(manifest->resolution, 64);
        QCOMPARE(manifest->license, u"CC-BY-4.0"_s);
        QVERIFY(manifest->procedural);
        QCOMPARE(manifest->preview, u"preview.png"_s);
        QCOMPARE(manifest->credits.size(), 1);
        QCOMPARE(manifest->credits.front().author, u"Ada"_s);
        QCOMPARE(manifest->credits.front().source, u"own work"_s);

        // Without "procedural" a pack only brings its own files.
        QVERIFY(!QcTexturePackManifest::parse(manifestBytes(32))->procedural);
    }

    void manifestRejectsBadValues_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("expected");
        QTest::newRow("not json") << QByteArray("{ nope") << u"not a valid JSON"_s;
        QTest::newRow("array") << QByteArray("[1, 2]") << u"not a valid JSON"_s;
        QTest::newRow("format 2") << manifestBytes(32, {{u"format"_s, 2}}) << u"Unsupported manifest format"_s;
        QTest::newRow("no name") << manifestBytes(32, {{u"name"_s, u"  "_s}}) << u"needs a 'name'"_s;
        QTest::newRow("name not text") << manifestBytes(32, {{u"name"_s, 5}}) << u"'name'"_s;
        QTest::newRow("long name") << manifestBytes(32, {{u"name"_s, QString(65, u'x')}}) << u"'name'"_s;
        QTest::newRow("resolution 48") << manifestBytes(48) << u"power of two"_s;
        QTest::newRow("resolution 8") << manifestBytes(8) << u"power of two"_s;
        QTest::newRow("resolution 1024") << manifestBytes(1024) << u"power of two"_s;
        QTest::newRow("resolution text") << manifestBytes(32, {{u"resolution"_s, u"32"_s}}) << u"power of two"_s;
        QTest::newRow("resolution fraction") << manifestBytes(32, {{u"resolution"_s, 32.5}}) << u"power of two"_s;
        QTest::newRow("preview traversal") << manifestBytes(32, {{u"preview"_s, u"../outside.png"_s}}) << u"'preview'"_s;
        QTest::newRow("preview in folder") << manifestBytes(32, {{u"preview"_s, u"textures/a.png"_s}}) << u"'preview'"_s;
        QTest::newRow("preview absolute") << manifestBytes(32, {{u"preview"_s, u"/etc/x.png"_s}}) << u"'preview'"_s;
        QTest::newRow("preview not png") << manifestBytes(32, {{u"preview"_s, u"preview.jpg"_s}}) << u"'preview'"_s;
        QTest::newRow("credits object") << manifestBytes(32, {{u"credits"_s, QJsonObject{}}}) << u"'credits'"_s;
        QTest::newRow("credit not object") << manifestBytes(32, {{u"credits"_s, QJsonArray{1}}}) << u"'credits'"_s;
        QJsonArray many;
        for (int i = 0; i < 33; ++i) {
            many.append(QJsonObject{{u"author"_s, u"a"_s}});
        }
        QTest::newRow("too many credits") << manifestBytes(32, {{u"credits"_s, many}}) << u"at most 32"_s;
        QTest::newRow("oversized") << manifestBytes(32, {{u"description"_s, QString(qsizetype(70) * 1024, u'x')}}) << u"larger than 64 KiB"_s;
    }

    void manifestRejectsBadValues()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, expected);
        const auto manifest = QcTexturePackManifest::parse(bytes);
        QVERIFY(!manifest.has_value());
        QVERIFY2(manifest.error().englishText().contains(expected), qPrintable(manifest.error().englishText()));
        QCOMPARE(manifest.error().file, u"texturepack.json"_s);
    }

    void packWithoutManifestCannotLoad()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/stone.png"_s), png(filled(16, 16, Qt::red))));
        const auto pack = QcTexturePackLoader::load(dir.path(), u"user:x"_s, QcTexturePack::Origin::User);
        QVERIFY(!pack.has_value());
        QVERIFY(pack.error().englishText().contains(u"no manifest"_s));
    }

    void imagesAndSpecsGetTheirNamesFromThePath()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32)));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/stone.png"_s), png(filled(32, 32, Qt::red))));
        QVERIFY(writeFile(dir.filePath(u"textures/rubies/ruby.json"_s), QJsonDocument(procedural("gem", "#c01020")).toJson()));

        const QcTexturePack pack = loadOk(dir.path());
        QVERIFY2(pack.problems.isEmpty(), qPrintable(messages(pack.problems)));
        QCOMPARE(pack.images.keys(), QList<QString>{u"qtcraft:stone"_s});
        QCOMPARE(pack.images.value(u"qtcraft:stone"_s).size(), QSize(32, 32));
        QCOMPARE(pack.images.value(u"qtcraft:stone"_s).pixelColor(5, 5), QColor(Qt::red));
        QCOMPARE(pack.specs.keys(), QList<QString>{u"rubies:ruby"_s});
        QCOMPARE(pack.specs.value(u"rubies:ruby"_s).value(u"pattern"_s).toString(), u"gem"_s);
        QCOMPARE(pack.decodedBytes, qint64(32 * 32 * 4));
    }

    void wrongSizeDamagedAndForeignFilesAreSkipped()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32)));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/small.png"_s), png(filled(16, 16, Qt::red))));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/wide.png"_s), png(filled(64, 32, Qt::red))));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/text.png"_s), "hello, not an image"));
        QByteArray truncated = png(filled(32, 32, Qt::green));
        truncated.chop(truncated.size() / 2);
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/truncated.png"_s), truncated));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/Upper Case.png"_s), png(filled(32, 32, Qt::red))));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/notes.txt"_s), "x"));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/broken.json"_s), "{ broken"));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/unknown.json"_s), QJsonDocument(procedural("spiral", "#ffffff")).toJson()));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/good.png"_s), png(filled(32, 32, Qt::blue))));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/.hidden.png"_s), png(filled(32, 32, Qt::red))));
        QVERIFY(writeFile(dir.filePath(u"textures/loose.png"_s), png(filled(32, 32, Qt::red))));

        const QcTexturePack pack = loadOk(dir.path());
        QCOMPARE(pack.images.keys(), QList<QString>{u"qtcraft:good"_s});
        QVERIFY(pack.specs.isEmpty());
        QVERIFY2(hasProblem(pack, u"textures/qtcraft/small.png"_s, u"16 x 16 pixels; this pack needs 32 x 32"_s), qPrintable(messages(pack.problems)));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/wide.png"_s, u"needs 32 x 32"_s));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/text.png"_s, u"Not a PNG"_s));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/truncated.png"_s, u"damaged"_s));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/Upper Case.png"_s, u"Not a texture file"_s));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/notes.txt"_s, u"Not a texture file"_s));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/broken.json"_s, u"Not a valid JSON"_s));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/unknown.json"_s, u"Invalid procedural texture"_s));
        QVERIFY(hasProblem(pack, u"textures/loose.png"_s, u"namespace folder"_s));
        // Hidden files are not listed at all.
        QVERIFY(!messages(pack.problems).contains(u".hidden"_s));
    }

    void linksOutOfThePackAreNotFollowed()
    {
        QTemporaryDir outside;
        QVERIFY(writeFile(outside.filePath(u"secret.png"_s), png(filled(32, 32, Qt::red))));
        QVERIFY(writeFile(outside.filePath(u"space/stone.png"_s), png(filled(32, 32, Qt::red))));

        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32)));
        QVERIFY(QDir().mkpath(dir.filePath(u"textures/qtcraft"_s)));
        QVERIFY(QFile::link(outside.filePath(u"secret.png"_s), dir.filePath(u"textures/qtcraft/dirt.png"_s)));
        QVERIFY(QFile::link(outside.filePath(u"space"_s), dir.filePath(u"textures/linked"_s)));

        const QcTexturePack pack = loadOk(dir.path());
        QVERIFY(pack.images.isEmpty());
        QVERIFY2(hasProblem(pack, u"textures/qtcraft/dirt.png"_s, u"Not a texture file"_s), qPrintable(messages(pack.problems)));
        QVERIFY(hasProblem(pack, u"textures/linked"_s, u"namespace folder"_s));

        // A linked manifest does not count either.
        QTemporaryDir linkedManifest;
        QVERIFY(writeFile(outside.filePath(u"texturepack.json"_s), manifestBytes(32)));
        QVERIFY(QFile::link(outside.filePath(u"texturepack.json"_s), linkedManifest.filePath(u"texturepack.json"_s)));
        QVERIFY(!QcTexturePackLoader::load(linkedManifest.path(), u"user:x"_s, QcTexturePack::Origin::User).has_value());
    }

    void limitsOfFilesSizeAndMemoryApply()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32)));
        for (const char *name : {"a", "b", "c", "d"}) {
            QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/%1.png"_s.arg(QLatin1StringView(name))), png(filled(32, 32, Qt::red))));
        }

        QcTexturePackLoader::Limits limits;
        limits.maxFiles = 3;
        limits.memoryBudget = qint64(2) * 32 * 32 * 4;
        const QcTexturePack pack = loadOk(dir.path(), limits);
        // Three files are read, the third exceeds the budget of two images, the fourth is not read.
        QCOMPARE(pack.images.size(), 2);
        QVERIFY(pack.images.contains(u"qtcraft:a"_s) && pack.images.contains(u"qtcraft:b"_s));
        QVERIFY2(hasProblem(pack, u"textures"_s, u"More than 3 texture files"_s), qPrintable(messages(pack.problems)));
        QVERIFY(hasProblem(pack, u"textures/qtcraft/c.png"_s, u"memory budget"_s));
        QCOMPARE(pack.decodedBytes, limits.memoryBudget);

        QcTexturePackLoader::Limits small;
        small.maxImageFileBytes = 64;
        const QcTexturePack tooLarge = loadOk(dir.path(), small);
        QVERIFY(tooLarge.images.isEmpty());
        QVERIFY(hasProblem(tooLarge, u"textures/qtcraft/a.png"_s, u"larger than 0 KiB"_s));
    }

    void animationStripsUseTheirFirstFrame()
    {
        QImage strip(32, 96, QImage::Format_RGBA8888);
        strip.fill(Qt::blue);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                strip.setPixelColor(x, y, Qt::yellow);
            }
        }
        const auto image = QcTexturePackLoader::readImage(png(strip), 32);
        QVERIFY(image.has_value());
        QCOMPARE(image->frames, 3);
        QCOMPARE(image->image.size(), QSize(32, 32));
        QCOMPARE(image->image.pixelColor(31, 31), QColor(Qt::yellow));

        QcTexturePackLoader::Limits limits;
        limits.maxFrames = 2;
        QVERIFY(!QcTexturePackLoader::readImage(png(strip), 32, limits).has_value());
        // Not a whole number of frames.
        QVERIFY(!QcTexturePackLoader::readImage(png(filled(32, 40, Qt::red)), 32).has_value());
    }

    void largeImagesAreStoredAtTheLimit()
    {
        QcTexturePackLoader::Limits limits;
        limits.maxStoredResolution = 32;
        const auto image = QcTexturePackLoader::readImage(png(filled(128, 128, QColor(10, 20, 30))), 128, limits);
        QVERIFY(image.has_value());
        QCOMPARE(image->image.size(), QSize(32, 32));
        QCOMPARE(image->image.pixelColor(16, 16), QColor(10, 20, 30));
    }

    void previewIsCheckedLikeTextures()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32, {{u"preview"_s, u"preview.png"_s}})));
        QCOMPARE(loadOk(dir.path()).problems.size(), 1);

        QVERIFY(writeFile(dir.filePath(u"preview.png"_s), png(filled(1024, 16, Qt::red))));
        QVERIFY(hasProblem(loadOk(dir.path()), u"preview.png"_s, u"at most 512"_s));

        QVERIFY(writeFile(dir.filePath(u"preview.png"_s), png(filled(128, 64, Qt::red))));
        const QcTexturePack pack = loadOk(dir.path());
        QVERIFY(pack.problems.isEmpty());
        QCOMPARE(pack.preview.size(), QSize(128, 64));
    }

    void libraryListsPacksWithTheirIds()
    {
        QTemporaryDir mods;
        QVERIFY(writeFile(mods.filePath(u"rubies/pack.json"_s), "{}"));
        QVERIFY(writeFile(mods.filePath(u"rubies/texturepacks/shiny/texturepack.json"_s), manifestBytes(64)));
        QVERIFY(writeFile(mods.filePath(u"rubies/texturepacks/broken/texturepack.json"_s), "{}"));

        const QList<QcTexturePackLibrary::Entry> entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots(mods.path()));
        const auto find = [&entries](const QString &id) { return std::ranges::find(entries, id, &QcTexturePackLibrary::Entry::id); };
        QVERIFY(find(u"builtin:qtcraft_hd_32"_s) != entries.end());
        QVERIFY(find(u"builtin:qtcraft_hd_64"_s) != entries.end());
        const auto shiny = find(u"mod:rubies/shiny"_s);
        QVERIFY(shiny != entries.end());
        QCOMPARE(shiny->origin, QcTexturePack::Origin::Mod);
        QCOMPARE(shiny->manifest->resolution, 64);
        const auto broken = find(u"mod:rubies/broken"_s);
        QVERIFY(broken != entries.end() && !broken->manifest && broken->error);

        const QcTexturePackLibrary::LoadedStack loaded =
            QcTexturePackLibrary::loadStack({u"mod:rubies/shiny"_s, u"mod:rubies/broken"_s, u"user:none"_s}, entries);
        QCOMPARE(loaded.stack.size(), 1);
        QCOMPARE(loaded.stack.front()->id, u"mod:rubies/shiny"_s);
        QCOMPARE(loaded.missing, (QStringList{u"mod:rubies/broken"_s, u"user:none"_s}));
    }

    // The checks above for the folders around the files: a linked textures folder, a linked pack
    // folder, a linked texturepacks folder of a mod and a linked preview are not followed.
    void linkedFoldersAndPreviewsAreNotFollowed()
    {
        QTemporaryDir outside;
        QVERIFY(writeFile(outside.filePath(u"textures/qtcraft/stone.png"_s), png(filled(32, 32, Qt::red))));
        QVERIFY(writeFile(outside.filePath(u"preview.png"_s), png(filled(64, 32, Qt::red))));
        QVERIFY(writeFile(outside.filePath(u"pack/texturepack.json"_s), manifestBytes(32)));

        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32, {{u"preview"_s, u"preview.png"_s}})));
        QVERIFY(QFile::link(outside.filePath(u"textures"_s), dir.filePath(u"textures"_s)));
        QVERIFY(QFile::link(outside.filePath(u"preview.png"_s), dir.filePath(u"preview.png"_s)));
        const QcTexturePack pack = loadOk(dir.path());
        QVERIFY(pack.images.isEmpty());
        QVERIFY(pack.preview.isNull());
        QVERIFY2(hasProblem(pack, u"textures"_s, u"links are not followed"_s), qPrintable(messages(pack.problems)));
        QVERIFY(hasProblem(pack, u"preview.png"_s, u"missing"_s));

        QTemporaryDir mods;
        QVERIFY(writeFile(mods.filePath(u"one/pack.json"_s), "{}"));
        QVERIFY(QFile::link(outside.path(), mods.filePath(u"one/texturepacks"_s)));
        QVERIFY(writeFile(mods.filePath(u"two/pack.json"_s), "{}"));
        QVERIFY(QDir().mkpath(mods.filePath(u"two/texturepacks"_s)));
        QVERIFY(QFile::link(outside.filePath(u"pack"_s), mods.filePath(u"two/texturepacks/linked"_s)));
        const QList<QcTexturePackLibrary::Entry> entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots(mods.path()));
        QVERIFY(std::ranges::none_of(entries, [](const QcTexturePackLibrary::Entry &entry) { return entry.origin == QcTexturePack::Origin::Mod; }));
    }

    // A folder with many files is read only up to twice the file limit, and the problems are capped.
    void endlessFoldersAndProblemsAreBounded()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(16)));
        for (int i = 0; i < 40; ++i) {
            QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/bad%1.txt"_s.arg(i)), "x"));
        }
        for (int i = 0; i < 5; ++i) {
            QVERIFY(writeFile(dir.filePath(u"textures/space%1/a.txt"_s.arg(i)), "x"));
        }

        QcTexturePackLoader::Limits limits;
        limits.maxFiles = 10;
        limits.maxNamespaces = 10;
        limits.maxProblems = 12;
        const QcTexturePack pack = loadOk(dir.path(), limits);
        QCOMPARE(pack.problems.size(), 13);
        QVERIFY(pack.problems.back().englishText().contains(u"more problems"_s));

        limits.maxProblems = 100;
        limits.maxNamespaces = 3;
        const QcTexturePack all = loadOk(dir.path(), limits);
        // 3 of the 6 namespace folders, up to 20 entries in each, plus the notes.
        QVERIFY2(all.problems.size() < 40, qPrintable(QString::number(all.problems.size())));
        QVERIFY(hasProblem(all, u"textures"_s, u"More than 3 entries"_s));
    }

    // A small file that claims a huge image is refused from its header, before decoding.
    void headerBombsAreRefusedBeforeDecoding()
    {
        const QByteArray bomb = headerBomb(100000, 100000);
        QVERIFY(bomb.size() < 100);
        QElapsedTimer timer;
        timer.start();
        const auto image = QcTexturePackLoader::readImage(bomb, 32);
        QVERIFY(!image.has_value());
        QVERIFY(timer.elapsed() < 1000);

        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32, {{u"preview"_s, u"preview.png"_s}})));
        QVERIFY(writeFile(dir.filePath(u"preview.png"_s), bomb));
        QVERIFY(hasProblem(loadOk(dir.path()), u"preview.png"_s, u"at most 512"_s));
        // A preview in another format is refused, even if Qt could read it.
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(filled(64, 32, Qt::red).save(&buffer, "BMP"));
        QVERIFY(writeFile(dir.filePath(u"preview.png"_s), jpeg));
        QVERIFY(hasProblem(loadOk(dir.path()), u"preview.png"_s, u"must be a PNG"_s));
    }

    void upperCaseSuffixesAreRefused()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(u"texturepack.json"_s), manifestBytes(32)));
        QVERIFY(writeFile(dir.filePath(u"textures/qtcraft/stone.PNG"_s), png(filled(32, 32, Qt::red))));
        const QcTexturePack pack = loadOk(dir.path());
        QVERIFY(pack.images.isEmpty());
        QVERIFY(hasProblem(pack, u"textures/qtcraft/stone.PNG"_s, u"Not a texture file"_s));
    }

private:
    QTemporaryDir m_data;
};

QTEST_GUILESS_MAIN(TestTexturePacks)
#include "test_texture_packs.moc"
