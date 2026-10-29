// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldLibraryController.h"

#include "app/debug/QcAppLogging.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcGameMode.h"
#include "worlds/QcModLoader.h"
#include "worlds/QcWorldLibrary.h"

#include <QDesktopServices>
#include <QDir>
#include <QLocale>
#include <QUrl>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace {

// The state of a mod as the mods screen shows it, and why it is not loaded.
struct ModState
{
    QString state;
    QString reason;
};

[[nodiscard]] ModState modState(const QcModStatus &mod, const QHash<QString, QString> &loadFailures)
{
    if (mod.state == QcModStatus::State::Disabled) {
        return {u"disabled"_s, mod.reason};
    }
    if (mod.state == QcModStatus::State::Failed) {
        return {u"failed"_s, mod.reason};
    }
    if (loadFailures.contains(mod.info.id)) {
        // Found fine on disk, but its content was refused when the last world started.
        return {u"failed"_s, loadFailures.value(mod.info.id)};
    }
    return {u"loaded"_s, mod.reason};
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
    const QDateTime now = QDateTime::currentDateTime();
    for (const QcWorldLibrary::Entry &entry : entries) {
        m_worlds.append(QVariantMap{{u"folder"_s, entry.folder},
                                    {u"name"_s, entry.info.name},
                                    {u"seed"_s, QString::number(entry.info.seed)},
                                    {u"generator"_s, entry.info.generator},
                                    {u"gameMode"_s, QString(QcGameModes::name(entry.info.defaultGameMode))},
                                    {u"lastPlayed"_s, playedText(entry.info.lastPlayed, now)},
                                    {u"error"_s, entry.tooOld ? tr("It was saved by an older development version of QtCraft and cannot be opened.")
                                                              : m_unplayable.value(entry.folder, entry.error)}});
    }
    emit worldsChanged();
}

QString QcWorldLibraryController::playedText(const QDateTime &played, const QDateTime &now)
{
    const QDateTime local = played.toLocalTime();
    const QDate today = now.toLocalTime().date();
    const QString time = QLocale().toString(local.time(), QLocale::ShortFormat);
    if (local.date() == today) {
        return tr("Today, %1").arg(time);
    }
    if (local.date() == today.addDays(-1)) {
        return tr("Yesterday, %1").arg(time);
    }
    return QLocale().toString(local.date(), QLocale::ShortFormat);
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
        const ModState state = modState(mod, m_modLoadFailures);
        m_mods.append(QVariantMap{{u"id"_s, mod.info.id},
                                  {u"name"_s, mod.info.name.isEmpty() ? QDir(mod.info.directory).dirName() : mod.info.name},
                                  {u"version"_s, mod.info.version},
                                  {u"description"_s, mod.info.description},
                                  {u"state"_s, state.state},
                                  {u"reason"_s, state.reason}});
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
        const QcModLoader::Loaded loaded = QcModLoader::load(m_modsDirectory);
        for (const QString &warning : QcModLoader::warningsOf(loaded)) {
            qCWarning(lcGame) << "Content:" << warning;
        }
        if (!loaded) {
            qCWarning(lcGame) << "Cannot load game content:" << loaded.error().error;
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
