#include "server/content/QcBundledMods.h"
#include "server/content/QcModLoader.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void writeText(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Relative path and SHA-256 of every regular file below `root`, sorted.
QStringList fingerprint(const QString &root)
{
    QStringList entries;
    QDirIterator files(root, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        const QString relative = QDir(root).relativeFilePath(path);
        if (relative.startsWith(u'.') || relative.contains(u"/."_s)) {
            continue;
        }
        entries.append(relative + u' ' + QString::fromLatin1(QCryptographicHash::hash(readAll(path), QCryptographicHash::Sha256).toHex()));
    }

    entries.sort();
    return entries;
}

QStringList idsIn(const QList<QcModStatus> &mods, QcModStatus::State state)
{
    QStringList ids;
    for (const QcModStatus &mod : mods) {
        if (mod.state == state) {
            ids.append(mod.info.id);
        }
    }
    return ids;
}

// Staging folders (`<mods>.installing-XXXXXX`) left beside `mods`.
QStringList stagingFolders(const QString &mods)
{
    const QFileInfo info(mods);
    return QDir(info.path()).entryList({info.fileName() + u".installing-*"_s}, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
}

const QString exampleMods = QStringLiteral(QTCRAFT_MODS_SOURCE_DIR);

} // namespace

class TestBundledMods : public QObject
{
    Q_OBJECT

private slots:
    void directoryInsideAndBesideTheProgram()
    {
        QTemporaryDir dir;

        // macOS bundle: the program in Contents/MacOS, the mods in Contents/Resources/mods.
        const QString contents = dir.filePath(u"QtCraft.app/Contents"_s);
        QVERIFY(QDir().mkpath(contents + u"/MacOS"_s));
        QCOMPARE(QcBundledMods::directory(contents + u"/MacOS"_s), QString());
        QVERIFY(QDir().mkpath(contents + u"/Resources/mods"_s));
        QCOMPARE(QcBundledMods::directory(contents + u"/MacOS"_s), QFileInfo(contents + u"/Resources/mods"_s).canonicalFilePath());

        // Other platforms: a mods folder next to the program.
        QVERIFY(QDir().mkpath(dir.filePath(u"bin/mods"_s)));
        QCOMPARE(QcBundledMods::directory(dir.filePath(u"bin"_s)), QFileInfo(dir.filePath(u"bin/mods"_s)).canonicalFilePath());

        // A linked mods folder is not used.
        QVERIFY(QDir().mkpath(dir.filePath(u"linked"_s)));
        QVERIFY(QFile::link(dir.filePath(u"bin/mods"_s), dir.filePath(u"linked/mods"_s)));
        QCOMPARE(QcBundledMods::directory(dir.filePath(u"linked"_s)), QString());
    }

    void firstStartInstallsTheExampleModSwitchedOff()
    {
        QTemporaryDir data;
        const QString mods = data.filePath(u"QtCraft/mods"_s);
        const QcResult<QStringList> installed = QcBundledMods::installDisabled(exampleMods, mods);
        QVERIFY2(installed, qPrintable(installed ? QString() : installed.error()));
        QCOMPARE(*installed, QStringList{u"rubies"_s});

        // A complete copy, nothing left beside it.
        QCOMPARE(fingerprint(mods + u"/rubies"_s), fingerprint(exampleMods + u"/rubies"_s));
        QVERIFY(stagingFolders(mods).isEmpty());

        // Readable for everyone like a folder the player creates, not owner-only like a temporary one.
        const QFileDevice::Permissions rights = QFileInfo(mods).permissions();
        QVERIFY(rights.testFlag(QFileDevice::ReadOther) && rights.testFlag(QFileDevice::ExeOther));
        QVERIFY(rights.testFlag(QFileDevice::ReadGroup) && !rights.testFlag(QFileDevice::WriteOther));

        // Listed but switched off: the base game alone loads.
        QCOMPARE(QcModLoader::disabledMods(mods), QStringList{u"rubies"_s});
        QStringList warnings;
        const QcResult<QcModLoader::Result> off = QcModLoader::load(mods, warnings);
        QVERIFY(off);
        QCOMPARE(idsIn(off->mods, QcModStatus::State::Disabled), QStringList{u"rubies"_s});
        QVERIFY(!off->content->blockId(u"rubies:ruby_ore"_s));

        // Switched on in the Mods screen, it loads.
        QVERIFY(QcModLoader::setModEnabled(mods, u"rubies"_s, true));
        warnings.clear();
        const QcResult<QcModLoader::Result> on = QcModLoader::load(mods, warnings);
        QVERIFY(on);
        QCOMPARE(idsIn(on->mods, QcModStatus::State::Loaded), QStringList{u"rubies"_s});
        QVERIFY(on->content->blockId(u"rubies:ruby_ore"_s));
    }

    void laterStartsLeaveThePlayersModsAlone()
    {
        QTemporaryDir data;
        const QString mods = data.filePath(u"mods"_s);
        QVERIFY(QcBundledMods::installDisabled(exampleMods, mods));

        // The player switched the mod on and removed a file; the next start changes nothing.
        QVERIFY(QcModLoader::setModEnabled(mods, u"rubies"_s, true));
        QVERIFY(QFile::remove(mods + u"/rubies/pack.json"_s));
        const QcResult<QStringList> again = QcBundledMods::installDisabled(exampleMods, mods);
        QVERIFY(again);
        QVERIFY(again->isEmpty());
        QVERIFY(QcModLoader::disabledMods(mods).isEmpty());
        QVERIFY(!QFileInfo::exists(mods + u"/rubies/pack.json"_s));

        // An existing, empty mods directory also counts as the player's choice.
        const QString empty = data.filePath(u"empty"_s);
        QVERIFY(QDir().mkpath(empty));
        QVERIFY(QcBundledMods::installDisabled(exampleMods, empty)->isEmpty());
        QVERIFY(QDir(empty).isEmpty());
    }

    void onlyRealModFoldersAreCopied()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(u"bundle"_s);
        writeText(source + u"/one/pack.json"_s, R"({"id": "one", "name": "One", "version": "1.0.0"})");
        writeText(source + u"/one/blocks/a.json"_s, "{}");
        writeText(source + u"/one/.DS_Store"_s, "x");
        writeText(dir.filePath(u"outside.txt"_s), "secret");
        QVERIFY(QFile::link(dir.filePath(u"outside.txt"_s), source + u"/one/blocks/leak.json"_s));
        writeText(source + u"/notes/readme.txt"_s, "no manifest");
        QVERIFY(QFile::link(source + u"/one"_s, source + u"/linked"_s));

        // A real folder whose manifest is a link.
        QVERIFY(QDir().mkpath(source + u"/linkedmanifest"_s));
        QVERIFY(QFile::link(source + u"/one/pack.json"_s, source + u"/linkedmanifest/pack.json"_s));

        // A folder of the player with a similar name stays untouched.
        const QString mods = dir.filePath(u"data/mods"_s);
        writeText(mods + u".installing/mine/pack.json"_s, "{}");

        const QcResult<QStringList> installed = QcBundledMods::installDisabled(source, mods);
        QVERIFY2(installed, qPrintable(installed ? QString() : installed.error()));
        QCOMPARE(*installed, QStringList{u"one"_s});

        QStringList entries = QDir(mods).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        entries.sort();
        QCOMPARE(entries, (QStringList{u"mods.json"_s, u"one"_s}));
        QCOMPARE(fingerprint(mods + u"/one"_s), fingerprint(source + u"/one"_s).filter(u"a.json"_s) + fingerprint(source + u"/one"_s).filter(u"pack.json"_s));
        QVERIFY(!QFileInfo::exists(mods + u"/one/blocks/leak.json"_s));
        QVERIFY(!QFileInfo::exists(mods + u"/one/.DS_Store"_s));
        QCOMPARE(readAll(mods + u".installing/mine/pack.json"_s), QByteArray("{}"));
        QVERIFY(stagingFolders(mods).isEmpty());
    }

    void nothingToInstall()
    {
        QTemporaryDir dir;
        const QString mods = dir.filePath(u"mods"_s);
        QVERIFY(QcBundledMods::installDisabled(QString(), mods)->isEmpty());
        QVERIFY(QDir().mkpath(dir.filePath(u"bundle"_s)));
        QVERIFY(QcBundledMods::installDisabled(dir.filePath(u"bundle"_s), mods)->isEmpty());
        // The player's directory is not created, so a later version can still install into it.
        QVERIFY(!QFileInfo::exists(mods));
    }

    void tooManyFilesLeaveNoTrace()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(u"bundle"_s);
        writeText(source + u"/big/pack.json"_s, R"({"id": "big", "name": "Big", "version": "1.0.0"})");
        for (int i = 0; i < QcBundledMods::maxFiles; ++i) {
            writeText(source + u"/big/blocks/b%1.json"_s.arg(i), "{}");
        }

        const QString mods = dir.filePath(u"mods"_s);
        const QcResult<QStringList> installed = QcBundledMods::installDisabled(source, mods);
        QVERIFY(!installed);
        QVERIFY(installed.error().contains(QString::number(QcBundledMods::maxFiles)));
        QVERIFY(!QFileInfo::exists(mods));
        QVERIFY(stagingFolders(mods).isEmpty());
    }

    void tooDeepLeavesNoTrace()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(u"bundle"_s);
        writeText(source + u"/deep/pack.json"_s, R"({"id": "deep", "name": "Deep", "version": "1.0.0"})");

        // maxDepth folders below the mod folder are copied, one more is refused.
        QString allowed = source + u"/deep"_s;
        for (int level = 0; level < QcBundledMods::maxDepth; ++level) {
            allowed += u"/d"_s;
        }
        writeText(allowed + u"/file.json"_s, "{}");
        const QString mods = dir.filePath(u"mods"_s);
        const QcResult<QStringList> installed = QcBundledMods::installDisabled(source, mods);
        QVERIFY2(installed, qPrintable(installed ? QString() : installed.error()));
        QCOMPARE(*installed, QStringList{u"deep"_s});

        writeText(allowed + u"/d/file.json"_s, "{}");
        const QString second = dir.filePath(u"second"_s);
        const QcResult<QStringList> refused = QcBundledMods::installDisabled(source, second);
        QVERIFY(!refused);
        QVERIFY(refused.error().contains(u"nested too deeply"_s));
        QVERIFY(!QFileInfo::exists(second));
        QVERIFY(stagingFolders(second).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBundledMods)
#include "test_bundled_mods.moc"
