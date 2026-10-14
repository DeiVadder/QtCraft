#include "QcWorldLibraryController.h"

#include "QcContentRegistry.h"
#include "QcModLoader.h"
#include "QcWorldLibrary.h"

#include <QDesktopServices>
#include <QDir>
#include <QLocale>
#include <QLoggingCategory>
#include <QUrl>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcGame, "qtcraft.game")

QString modState(const QcModStatus &mod, const QHash<QString, QString> &loadFailures, QString &reason)
{
    reason = mod.reason;
    if (mod.state == QcModStatus::State::Disabled) {
        return u"disabled"_s;
    }
    if (mod.state == QcModStatus::State::Failed) {
        return u"failed"_s;
    }
    if (loadFailures.contains(mod.info.id)) {
        // Found fine on disk, but its content was refused when the last world started.
        reason = loadFailures.value(mod.info.id);
        return u"failed"_s;
    }
    return u"loaded"_s;
}

} // namespace

QcWorldLibraryController::QcWorldLibraryController(QObject *parent)
    : QObject(parent)
{
}

QcWorldLibraryController::~QcWorldLibraryController() = default;

void QcWorldLibraryController::refreshWorlds()
{
    m_worlds.clear();
    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QList<QcWorldLibrary::Entry> entries = library.list();
    for (const QcWorldLibrary::Entry &entry : entries) {
        m_worlds.append(QVariantMap{{u"folder"_s, entry.folder},
                                    {u"name"_s, entry.info.name},
                                    {u"seed"_s, QString::number(entry.info.seed)},
                                    {u"generator"_s, entry.info.generator},
                                    {u"lastPlayed"_s, QLocale().toString(entry.info.lastPlayed.toLocalTime(), QLocale::ShortFormat)},
                                    {u"error"_s, entry.tooOld ? tr("It was saved by an older development version of QtCraft and cannot be opened.")
                                                              : m_unplayable.value(entry.folder, entry.error)}});
    }
    emit worldsChanged();
}

void QcWorldLibraryController::noteUnplayable(const QString &folder, const QString &message)
{
    if (message.isEmpty() && !m_unplayable.contains(folder)) {
        return;
    }
    if (message.isEmpty()) {
        m_unplayable.remove(folder);
    } else {
        m_unplayable.insert(folder, message);
    }
    refreshWorlds();
}

void QcWorldLibraryController::deleteWorld(const QString &folder)
{
    const QcStatus removed = QcWorldLibrary(QcWorldLibrary::defaultRoot()).remove(folder);
    if (!removed) {
        qCWarning(lcGame) << removed.error();
    }
    refreshWorlds();
}

void QcWorldLibraryController::setModsDirectory(const QString &directory)
{
    m_modsDirectory = directory;
    refreshMods();
    resetContent();
}

void QcWorldLibraryController::refreshMods()
{
    m_mods.clear();
    const QList<QcModStatus> mods = QcModLoader::discover(m_modsDirectory);
    for (const QcModStatus &mod : mods) {
        QString reason;
        const QString state = modState(mod, m_modLoadFailures, reason);
        m_mods.append(QVariantMap{{u"id"_s, mod.info.id},
                                  {u"name"_s, mod.info.name.isEmpty() ? QDir(mod.info.directory).dirName() : mod.info.name},
                                  {u"version"_s, mod.info.version},
                                  {u"description"_s, mod.info.description},
                                  {u"state"_s, state},
                                  {u"reason"_s, reason}});
    }
    emit modsChanged();
}

void QcWorldLibraryController::setModEnabled(const QString &id, bool enabled)
{
    if (const QcStatus written = QcModLoader::setModEnabled(m_modsDirectory, id, enabled); !written) {
        qCWarning(lcGame) << "Cannot change the mod settings:" << written.error();
    }
    refreshMods();
    resetContent();
}

void QcWorldLibraryController::openModsFolder()
{
    if (!m_modsDirectory.isEmpty() && QDir().mkpath(m_modsDirectory)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_modsDirectory));
    }
}

void QcWorldLibraryController::recordLoadedMods(const QList<QcModStatus> &mods)
{
    m_modLoadFailures.clear();
    QStringList loadedMods;
    for (const QcModStatus &mod : mods) {
        if (mod.state == QcModStatus::State::Failed && !mod.info.id.isEmpty()) {
            m_modLoadFailures.insert(mod.info.id, mod.reason);
        } else if (mod.state == QcModStatus::State::Loaded) {
            loadedMods.append(mod.info.id + u' ' + mod.info.version);
        }
    }
    qCInfo(lcGame).noquote() << u"Mods loaded: %1"_s.arg(loadedMods.isEmpty() ? u"none"_s : loadedMods.join(u", "_s));

    refreshMods();
}

const QcContentRegistry &QcWorldLibraryController::content() const
{
    if (!m_content) {
        QStringList warnings;
        QcResult<QcModLoader::Result> loaded = QcModLoader::load(m_modsDirectory, warnings);
        for (const QString &warning : std::as_const(warnings)) {
            qCWarning(lcGame) << "Content:" << warning;
        }
        m_content = loaded ? std::shared_ptr<const QcContentRegistry>(loaded->content) : std::make_shared<const QcContentRegistry>();
    }
    return *m_content;
}

void QcWorldLibraryController::useContent(std::shared_ptr<const QcContentRegistry> content)
{
    m_content = std::move(content);
    ++m_contentGeneration;
}

void QcWorldLibraryController::resetContent()
{
    m_content.reset();
    ++m_contentGeneration;
    emit contentReset();
}
