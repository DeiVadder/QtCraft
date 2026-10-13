#include "QcTexturePackController.h"

#include "QcGameSettings.h"
#include "QcGpuLimits.h"
#include "QcTextureAtlas.h"
#include "QcTexturePackLoader.h"
#include "QcTexturePackPreviewProvider.h"

#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcTextures, "qtcraft.textures")

QString originName(QcTexturePack::Origin origin)
{
    switch (origin) {
    case QcTexturePack::Origin::BuiltIn:
        return u"builtin"_s;
    case QcTexturePack::Origin::Mod:
        return u"mod"_s;
    case QcTexturePack::Origin::User:
        break;
    }
    return u"user"_s;
}

QString creditsText(const QList<QcTexturePackManifest::Credit> &credits)
{
    QStringList lines;
    for (const QcTexturePackManifest::Credit &credit : credits) {
        QStringList details;
        for (const QString &part : {credit.license, credit.source, credit.note}) {
            if (!part.isEmpty()) {
                details.append(part);
            }
        }
        lines.append(details.isEmpty() ? credit.author : u"%1 (%2)"_s.arg(credit.author, details.join(u", "_s)));
    }
    return lines.join(u'\n');
}

} // namespace

QcTexturePackController::QcTexturePackController(std::function<QString()> modsDirectory, QObject *parent)
    : QObject(parent)
    , m_modsDirectory(std::move(modsDirectory))
{
    connect(&m_atlasBuild, &QFutureWatcher<QcTextureStackBuild::Result>::finished, this, &QcTexturePackController::onBuilt);
}

QcTexturePackController::~QcTexturePackController()
{
    // The job only holds its own copies, but no thread may outlive the application.
    m_atlasBuild.waitForFinished();
}

void QcTexturePackController::overrideSelection(const QStringList &ids)
{
    m_active = QcGameSettings::cleanTexturePacks(ids);
    m_overridden = true;
}

void QcTexturePackController::loadSelection(const QStringList &ids)
{
    m_stored = QcGameSettings::cleanTexturePacks(ids);
    if (!m_overridden) {
        m_active = m_stored;
    }
    refresh();
}

void QcTexturePackController::refresh()
{
    // Packs changed on disk are read again; a build still running is out of date.
    ++m_generation;
    m_rescan = true;
    m_loaded.clear();
    startBuild();
}

std::shared_ptr<const QcTextureAtlas> QcTexturePackController::enterWorld(std::shared_ptr<const QcContentRegistry> content)
{
    ++m_generation;
    m_buildAgain = false;
    m_content = std::move(content);
    const QcTextureStackBuild::Result result = QcTextureStackBuild::run(request());
    apply(result);
    describe(*result.atlas, result.loadMs + result.buildMs);
    return result.atlas;
}

void QcTexturePackController::leaveWorld()
{
    ++m_generation;
    m_buildAgain = false;
    m_content.reset();
}

QString QcTexturePackController::userDirectory() const
{
    return QcTexturePackLibrary::userDirectory();
}

void QcTexturePackController::setActive(const QString &id, bool active)
{
    select(QcTexturePackLibrary::withActive(m_active, id, active));
}

void QcTexturePackController::move(const QString &id, int steps)
{
    select(QcTexturePackLibrary::withMoved(m_active, id, steps));
}

void QcTexturePackController::openFolder() const
{
    if (QDir().mkpath(userDirectory())) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(userDirectory()));
    }
}

void QcTexturePackController::select(const QStringList &ids)
{
    if (activate(ids)) {
        m_overridden = false;
        emit selectionChanged();
    }
}

void QcTexturePackController::selectForRun(const QStringList &ids)
{
    if (!m_overridden) {
        m_stored = m_active;
        m_overridden = true;
    }
    activate(ids);
}

bool QcTexturePackController::activate(const QStringList &ids)
{
    const QStringList clean = QcGameSettings::cleanTexturePacks(ids);
    if (clean == m_active) {
        return false;
    }

    m_selectionNotices.clear();
    for (const QString &id : ids) {
        if (!clean.contains(id)) {
            m_selectionNotices.append(tr("At most %1 texture packs can be active; %2 was switched off").arg(QcGameSettings::maxTexturePacks).arg(id));
        }
    }
    ++m_generation;
    m_active = clean;
    emit packsChanged();
    startBuild();
    return true;
}

QcTextureStackBuild::Request QcTexturePackController::request() const
{
    QcTextureStackBuild::Request request;
    request.generation = m_generation;
    request.ids = m_active;
    if (m_rescan) {
        request.rescan = QcTexturePackLibrary::roots(m_modsDirectory ? m_modsDirectory() : QString());
    }
    request.entries = m_entries;
    request.loaded = m_loaded;
    request.previews = m_previews;
    request.content = m_content;
    request.options.maxExtent = QcGpuLimits::maxTextureSize();
    return request;
}

void QcTexturePackController::startBuild()
{
    if (m_atlasBuild.isRunning()) {
        m_buildAgain = true;
        emit statusChanged();
        return;
    }

    m_atlasBuild.setFuture(QtConcurrent::run(&QcTextureStackBuild::run, request()));
    emit statusChanged();
}

void QcTexturePackController::onBuilt()
{
    const QcTextureStackBuild::Result result = m_atlasBuild.result();
    if (m_buildAgain) {
        m_buildAgain = false;
        startBuild();
    }
    // A result of an older generation (another selection, Reload, another world or none) is
    // dropped: applying it would undo the later change.
    if (result.generation != m_generation) {
        emit statusChanged();
        return;
    }

    apply(result);
    if (result.atlas && m_content) {
        describe(*result.atlas, result.loadMs + result.buildMs);
        emit atlasBuilt(result.atlas);
    }
}

void QcTexturePackController::apply(const QcTextureStackBuild::Result &result)
{
    m_entries = result.entries;
    m_rescan = false;
    m_stack = result.stack;
    m_previews = result.previews;
    // Only the active packs stay loaded.
    m_loaded.clear();
    for (const QcTexturePackRef &pack : std::as_const(m_stack)) {
        m_loaded.insert(pack->id, pack);
    }

    m_notices = m_selectionNotices;
    for (const QString &id : result.missing) {
        m_notices.append(tr("The texture pack %1 is not available or its manifest is invalid").arg(id));
    }
    for (const QString &id : result.overBudget) {
        m_notices.append(tr("The texture pack %1 was left out: the active packs together need more memory than allowed").arg(id));
    }
    updatePreviews();
    emit packsChanged();
    emit statusChanged();
    emit stackChanged();
}

void QcTexturePackController::describe(const QcTextureAtlas &atlas, qint64 milliseconds)
{
    const double mebibytes = double(atlas.bytes()) / (1024.0 * 1024.0);
    m_status = tr("%1x textures: atlas %2 x %3 pixels with %4 mip levels, %5 MiB, ready in %6 ms")
                   .arg(atlas.tileSize())
                   .arg(atlas.image().width())
                   .arg(atlas.image().height())
                   .arg(atlas.levels().size())
                   .arg(mebibytes, 0, 'f', 1)
                   .arg(milliseconds);
    const QcTexturePackProblems notices = atlas.notices();
    for (const QcTexturePackProblem &notice : notices) {
        m_notices.append(notice.text());
    }
    qCInfo(lcTextures).noquote() << "Texture atlas:" << atlas.tileSize() << "x," << atlas.image().width() << "x" << atlas.image().height() << "px,"
                                 << atlas.levels().size() << "levels," << QString::number(mebibytes, 'f', 1) << "MiB," << milliseconds
                                 << "ms; packs:" << (m_active.isEmpty() ? u"none"_s : m_active.join(u", "_s));
    emit statusChanged();
}

void QcTexturePackController::updatePreviews()
{
    QHash<QString, QImage> previews;
    for (const QcTexturePackLibrary::Entry &entry : std::as_const(m_entries)) {
        previews.insert(entry.id, m_previews.value(entry.directory).image);
    }
    ++m_previewGeneration;
    if (m_previewProvider) {
        m_previewProvider->setPreviews(std::move(previews));
    }
}

QVariantList QcTexturePackController::packs() const
{
    QList<const QcTexturePackLibrary::Entry *> ordered;
    for (const QString &id : m_active) {
        const auto entry = std::ranges::find(m_entries, id, &QcTexturePackLibrary::Entry::id);
        if (entry != m_entries.end()) {
            ordered.append(&*entry);
        }
    }
    for (const QcTexturePackLibrary::Entry &entry : m_entries) {
        if (!m_active.contains(entry.id)) {
            ordered.append(&entry);
        }
    }

    QVariantList list;
    for (const QcTexturePackLibrary::Entry *entry : std::as_const(ordered)) {
        QStringList problems;
        if (const QcTexturePackRef loaded = m_loaded.value(entry->id)) {
            for (const QcTexturePackProblem &problem : loaded->problems) {
                problems.append(problem.text());
            }
        }
        const QcTexturePackManifest manifest = entry->manifest.value_or(QcTexturePackManifest{});
        list.append(QVariantMap{
            {u"id"_s, entry->id},
            {u"name"_s, manifest.name.isEmpty() ? entry->id.section(u':', 1) : manifest.name},
            {u"description"_s, manifest.description},
            {u"resolution"_s, entry->manifest ? manifest.resolution : 0},
            {u"license"_s, manifest.license},
            {u"credits"_s, creditsText(manifest.credits)},
            {u"origin"_s, originName(entry->origin)},
            {u"active"_s, m_active.contains(entry->id)},
            {u"error"_s, entry->error ? entry->error->text() : QString()},
            {u"problems"_s, problems},
            {u"preview"_s, manifest.preview.isEmpty() ? QString() : u"image://texturepacks/%1?%2"_s.arg(QString::fromLatin1(QUrl::toPercentEncoding(entry->id))).arg(m_previewGeneration)},
        });
    }
    return list;
}
