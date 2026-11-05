// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcGeneratorUpgradeController.h"

#include "app/debug/QcAppLogging.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/world/QcWorldInfo.h"
#include "worlds/QcWorldLibrary.h"

#include <QVariantMap>

using namespace Qt::StringLiterals;

// The progress of the switching thread goes over by number.
static_assert(static_cast<int>(QcGeneratorUpgradeController::Stage::Backup) == static_cast<int>(QcConversionControl::Stage::Backup));
static_assert(static_cast<int>(QcGeneratorUpgradeController::Stage::Survey) == static_cast<int>(QcConversionControl::Stage::Survey));
static_assert(static_cast<int>(QcGeneratorUpgradeController::Stage::Write) == static_cast<int>(QcConversionControl::Stage::Write));

QcGeneratorUpgradeController::QcGeneratorUpgradeController(std::function<std::shared_ptr<const QcContentRegistry>()> content, QObject *parent)
    : QObject(parent)
    , m_content(std::move(content))
{
}

QcGeneratorUpgradeController::~QcGeneratorUpgradeController()
{
    m_cancel.store(true);
    stopThread();
}

QVariantList QcGeneratorUpgradeController::changes() const
{
    const std::shared_ptr<const QcContentRegistry> content = m_content();
    const QString language = m_language ? m_language() : QString();
    QVariantList list;
    for (const QcGeneratorUpgrade::Change &change : m_changes) {
        const bool overworld = change.dimension == QLatin1StringView(QcDimensionDefinition::overworldName);
        const QcDimensionDefinition *dimension = overworld ? &content->worldgen().overworld() : content->worldgen().dimension(change.dimension);
        const QString name = dimension != nullptr ? content->translations().translate(dimension->translationKey, language) : change.dimension;
        list.append(QVariantMap{{u"name"_s, name}, {u"from"_s, change.from}, {u"to"_s, change.to}});
    }
    return list;
}

QString QcGeneratorUpgradeController::changesText() const
{
    QStringList parts;
    const QVariantList list = changes();
    for (const QVariant &change : list) {
        const QVariantMap map = change.toMap();
        parts.append(tr("%1: version %2 → %3").arg(map.value(u"name"_s).toString()).arg(map.value(u"from"_s).toInt()).arg(map.value(u"to"_s).toInt()));
    }
    return parts.join(u", "_s);
}

QString QcGeneratorUpgradeController::stageText() const
{
    if (cancelling()) {
        return tr("Cancelling …");
    }
    switch (stage()) {
    case Stage::Backup:
        return tr("Backing up the world …");
    case Stage::Survey:
        return tr("Surveying the explored land and its structures …");
    case Stage::Write:
        return tr("Writing the new version …");
    }
    return {};
}

bool QcGeneratorUpgradeController::offer(const QString &folder, const QString &directory, const QcWorldInfo &info)
{
    if (state() == State::Converting) {
        qCWarning(lcGame) << "Generator switch: not opening" << folder << "while" << m_folder << "is switched";
        return true;
    }
    if (m_kept.contains(folder)) {
        return false;
    }
    const std::shared_ptr<const QcContentRegistry> content = m_content();
    QList<QcGeneratorUpgrade::Change> changes = QcGeneratorUpgrade::outdatedIn(info, *content);
    if (changes.isEmpty() || QcGeneratorUpgrade::isDeclined(info, changes)) {
        return false;
    }
    m_folder = folder;
    m_directory = directory;
    m_worldName = info.name;
    m_changes = std::move(changes);
    setError({});
    setState(State::Asking);
    return true;
}

void QcGeneratorUpgradeController::keep(bool forGood)
{
    if (state() != State::Asking) {
        qCWarning(lcGame) << "Generator switch: keep ignored in state" << state();
        return;
    }
    if (forGood) {
        if (const QcStatus kept = QcGeneratorUpgrade::keepForGood(m_directory, m_changes, m_content()); !kept) {
            qCWarning(lcGame) << "Generator switch: cannot keep the version for good:" << kept.error();
        }
    }
    // Asked once per run of the game: the next opening of this world plays at once.
    m_kept.insert(m_folder);
    setState(State::Idle);
    emit playRequested(m_folder);
}

void QcGeneratorUpgradeController::switchHard()
{
    if (state() != State::Asking) {
        qCWarning(lcGame) << "Generator switch: switch ignored in state" << state();
        return;
    }
    stopThread();
    m_cancel.store(false);
    setProgress(Stage::Backup, -1.0);
    setState(State::Converting);

    const QcGeneratorUpgrade::Request request{m_directory, m_changes, QcGeneratorUpgrade::Mode::Hard, {}};
    const QcConversionControl control{[this](const QcConversionControl::Step &step) { report(step); }, [this] { return m_cancel.load(); }};
    m_thread.reset(QThread::create([this, request, control, content = m_content()] {
        const QcResult<QcGeneratorUpgrade::Converted> result = QcGeneratorUpgrade::convert(request, content, control);
        QMetaObject::invokeMethod(this, [this, result] { finish(result); }, Qt::QueuedConnection);
    }));
    m_thread->setObjectName(u"generator switch"_s);
    m_thread->start();
}

void QcGeneratorUpgradeController::askAgain()
{
    QStringList directories;
    const QList<QcWorldLibrary::Entry> entries = QcWorldLibrary(QcWorldLibrary::defaultRoot()).list();
    for (const QcWorldLibrary::Entry &entry : entries) {
        directories.append(entry.directory);
    }
    m_kept.clear();
    m_askedAgain = QcGeneratorUpgrade::askAgain(directories, m_content());
    emit askedAgainChanged();
}

void QcGeneratorUpgradeController::cancel()
{
    if (state() != State::Converting) {
        qCWarning(lcGame) << "Generator switch: cancel ignored in state" << state();
        return;
    }
    m_cancel.store(true);
    emit progressChanged();
}

void QcGeneratorUpgradeController::dismiss()
{
    if (state() != State::Failed) {
        qCWarning(lcGame) << "Generator switch: dismiss ignored in state" << state();
        return;
    }
    setState(State::Idle);
}

void QcGeneratorUpgradeController::preview(Stage stage, double progress)
{
    m_worldName = tr("Preview");
    m_changes = {{QString::fromLatin1(QcDimensionDefinition::overworldName), QcGeneratorVersion::current - 1, QcGeneratorVersion::current}};
    setProgress(stage, progress);
    setState(State::Converting);
}

void QcGeneratorUpgradeController::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void QcGeneratorUpgradeController::setError(const QString &error)
{
    m_error = error;
}

void QcGeneratorUpgradeController::report(const QcConversionControl::Step &step)
{
    const double share = step.total > 0 ? static_cast<double>(step.done) / static_cast<double>(step.total) : -1.0;
    const auto stage = static_cast<Stage>(step.stage);
    QMetaObject::invokeMethod(this, [this, stage, share] { setProgress(stage, share); }, Qt::QueuedConnection);
}

void QcGeneratorUpgradeController::setProgress(Stage stage, double progress)
{
    m_stage = stage;
    m_progress = progress;
    emit progressChanged();
}

void QcGeneratorUpgradeController::finish(const QcResult<QcGeneratorUpgrade::Converted> &result)
{
    stopThread();
    if (!result) {
        qCWarning(lcGame) << "Generator switch failed:" << result.error();
        setError(result.error());
        setState(State::Failed);
        return;
    }
    if (result->cancelled) {
        // The world is as it was; the question comes back.
        qCInfo(lcGame) << "Generator switch cancelled; the world is unchanged";
        setState(State::Asking);
        return;
    }
    for (const QString &warning : result->warnings) {
        qCWarning(lcGame) << "Generator switch:" << warning;
    }
    setState(State::Idle);
    emit playRequested(m_folder);
}

void QcGeneratorUpgradeController::stopThread()
{
    if (m_thread) {
        m_thread->wait();
        m_thread.reset();
    }
}
