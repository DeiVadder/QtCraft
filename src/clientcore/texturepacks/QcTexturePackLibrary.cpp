#include "clientcore/texturepacks/QcTexturePackLibrary.h"

#include "clientcore/texturepacks/QcTexturePackLoader.h"
#include "common/core/QcDataRoot.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcTexturePackLibrary {

namespace {

QFileInfoList folders(const QString &path)
{
    return QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

} // namespace

QString userDirectory()
{
    return QDir(QcDataRoot::path()).filePath(u"texturepacks"_s);
}

QList<Root> roots(const QString &modsDirectory)
{
    QList<Root> roots = {
        {QString::fromLatin1(builtInDirectory), QcTexturePack::Origin::BuiltIn, u"builtin:"_s},
        {userDirectory(), QcTexturePack::Origin::User, u"user:"_s},
    };
    if (modsDirectory.isEmpty()) {
        return roots;
    }

    const QFileInfoList mods = folders(modsDirectory);
    for (const QFileInfo &mod : mods) {
        const QString packs = QDir(mod.filePath()).filePath(u"texturepacks"_s);
        // Neither the mod folder nor its texturepacks folder may be a link out of the mods folder.
        if (!mod.isSymLink() && QFileInfo(packs).isDir() && !QFileInfo(packs).isSymLink()) {
            roots.append({packs, QcTexturePack::Origin::Mod, u"mod:%1/"_s.arg(mod.fileName())});
        }
    }
    return roots;
}

QList<Entry> scan(const QList<Root> &roots)
{
    QList<Entry> entries;
    for (const Root &root : roots) {
        const QFileInfoList packs = folders(root.path);
        for (const QFileInfo &folder : packs) {
            if (folder.isSymLink() || !QcTexturePackManifest::isPlainFileName(folder.fileName()) || entries.size() >= maxPacks) {
                continue;
            }

            Entry entry{root.idPrefix + folder.fileName(), root.origin, folder.filePath(), std::nullopt, std::nullopt};
            std::expected<QcTexturePackManifest, QcTexturePackProblem> manifest = QcTexturePackLoader::readManifest(entry.directory);
            if (manifest) {
                entry.manifest = std::move(*manifest);
            } else {
                entry.error = manifest.error();
            }
            entries.append(std::move(entry));
        }
    }
    return entries;
}

QcTexturePackStack loadStack(const QStringList &ids, const QList<Entry> &entries, QStringList &missing)
{
    QcTexturePackStack stack;
    for (const QString &id : ids) {
        const auto entry = std::ranges::find(entries, id, &Entry::id);
        std::expected<QcTexturePack, QcTexturePackProblem> pack = entry != entries.end()
                                                                     ? QcTexturePackLoader::load(entry->directory, id, entry->origin)
                                                                     : std::unexpected(QcTexturePackProblem{});
        if (!pack) {
            missing.append(id);
            continue;
        }
        stack.append(std::make_shared<const QcTexturePack>(std::move(*pack)));
    }
    return stack;
}

QStringList withActive(QStringList active, const QString &id, bool on)
{
    active.removeAll(id);
    if (on && !id.isEmpty()) {
        active.prepend(id);
    }
    return active;
}

QStringList withMoved(QStringList active, const QString &id, int steps)
{
    const qsizetype from = active.indexOf(id);
    if (from < 0) {
        return active;
    }
    active.move(from, std::clamp<qsizetype>(from + steps, 0, active.size() - 1));
    return active;
}

} // namespace QcTexturePackLibrary
