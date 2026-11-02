// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldLibraryController.h"

#include "app/debug/QcAppLogging.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcGameMode.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcModLoader.h"
#include "worlds/QcWorldLibrary.h"

#include <QDesktopServices>
#include <QDir>
#include <QLocale>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The state of a mod as the mods screen shows it, and why it is not loaded.
struct ModState
{
    QcModStatus::State state;
    QString reason;
};

[[nodiscard]] ModState modState(const QcModStatus &mod, const QHash<QString, QString> &loadFailures)
{
    if (mod.state == QcModStatus::State::Loaded && loadFailures.contains(mod.info.id)) {
        // Found fine on disk, but its content was refused when the last world started.
        return {QcModStatus::State::Failed, loadFailures.value(mod.info.id)};
    }
    return {mod.state, mod.reason};
}

// The block whose icon stands for a world's type on its tile in the main menu (HM-3 of
// docs/Menue-Pruefung.md): a slab for a flat world, glass for the void, a bookshelf for the
// showcase and grass for every other.
[[nodiscard]] QString typeBlock(const QString &generator)
{
    static const QHash<QString, QString> blocks{{u"flat"_s, u"qtcraft:smooth_stone_slab"_s},
                                                {u"void"_s, u"qtcraft:glass"_s},
                                                {u"showcase"_s, u"qtcraft:bookshelf"_s}};
    return blocks.value(generator, u"qtcraft:grass_block"_s);
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
                                    {u"worldType"_s, static_cast<int>(worldTypeOf(entry.info.generator))},
                                    {u"typeBlock"_s, typeBlock(entry.info.generator)},
                                    {u"gameMode"_s, static_cast<int>(entry.info.defaultGameMode)},
                                    {u"lastPlayed"_s, playedText(entry.info.lastPlayed, now)},
                                    {u"error"_s, entry.tooOld ? tr("It was saved by an older development version of QtCraft and cannot be opened.")
                                                              : m_unplayable.value(entry.folder, entry.error)}});
    }
    emit worldsChanged();
}

namespace {

// The index of the world "Continue" opens in `worlds`; -1 when none can be opened.
[[nodiscard]] qsizetype continueIndex(const QVariantList &worlds)
{
    const auto found = std::ranges::find_if(worlds, [](const QVariant &world) { return world.toMap().value(u"error"_s).toString().isEmpty(); });
    return found == worlds.end() ? -1 : std::distance(worlds.begin(), found);
}

} // namespace

QVariant QcWorldLibraryController::continueWorld() const
{
    const qsizetype index = continueIndex(worlds());
    // A null QML sees as null, not undefined.
    return index < 0 ? QVariant::fromValue(nullptr) : worlds().at(index);
}

QVariantList QcWorldLibraryController::otherWorlds() const
{
    QVariantList others = worlds();
    if (const qsizetype index = continueIndex(others); index >= 0) {
        others.removeAt(index);
    }
    return others;
}

QLatin1StringView QcWorldLibraryController::generatorOf(WorldType type)
{
    switch (type) {
    case WorldType::Flat:
        return QLatin1StringView(QcWorldGenerators::flat);
    case WorldType::Void:
        return QLatin1StringView(QcWorldGenerators::empty);
    case WorldType::Showcase:
        return QLatin1StringView(QcWorldGenerators::showcase);
    case WorldType::Terrain:
        break;
    }
    return QLatin1StringView(QcWorldGenerators::terrain);
}

QcWorldLibraryController::WorldType QcWorldLibraryController::worldTypeOf(const QString &generator)
{
    for (const WorldType type : {WorldType::Flat, WorldType::Void, WorldType::Showcase}) {
        if (generator == generatorOf(type)) {
            return type;
        }
    }
    return WorldType::Terrain;
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
    const QList<QcModStatus> mods = QcModLoader::discover(modsDirectory());
    for (const QcModStatus &mod : mods) {
        const ModState state = modState(mod, m_modLoadFailures);
        m_mods.append(QVariantMap{{u"id"_s, mod.info.id},
                                  {u"name"_s, mod.info.name.isEmpty() ? QDir(mod.info.directory).dirName() : mod.info.name},
                                  {u"version"_s, mod.info.version},
                                  {u"description"_s, mod.info.description},
                                  {u"state"_s, static_cast<int>(state.state)},
                                  {u"reason"_s, state.reason}});
    }
    emit modsChanged();
}

void QcWorldLibraryController::setModEnabled(const QString &id, bool enabled)
{
    if (const QcStatus written = QcModLoader::setModEnabled(modsDirectory(), id, enabled); !written) {
        qCWarning(lcGame) << "Cannot change the mod settings:" << written.error();
    }
    refreshMods();
    resetContent();
}

void QcWorldLibraryController::openWorldFolder(const QString &folder) const
{
    // Only a world folder of the library: no path out of it, no link, no other directory.
    const QcResult<QString> directory = QcWorldLibrary(QcWorldLibrary::defaultRoot()).worldDirectory(folder);
    if (!directory) {
        qCWarning(lcGame) << "Not opening the world folder:" << directory.error();
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(*directory))) {
        qCWarning(lcGame) << "Cannot open the world folder" << *directory;
    }
}

void QcWorldLibraryController::openModsFolder() const
{
    if (!modsDirectory().isEmpty() && QDir().mkpath(modsDirectory())) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(modsDirectory()));
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
        const QcModLoader::Loaded loaded = QcModLoader::load(modsDirectory());
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
