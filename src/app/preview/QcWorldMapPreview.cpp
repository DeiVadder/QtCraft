#include "app/preview/QcWorldMapPreview.h"

#include "app/debug/QcAppLogging.h"
#include "app/models/QcWorldMapImageProvider.h"
#include "app/preview/QcWorldMapTexts.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorkerPool.h"
#include "worlds/QcWorldLibrary.h"

#include <QColor>
#include <QHash>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The view's centre stays this far from the world's origin (far inside the world's edge).
constexpr int maxCentre = 1 << 24;
// The passes that draw the map: coarse and fine (the sites come after them).
constexpr int mapPasses = 2;

// The shapes of QcWorldMapPreview::MarkerShape.
constexpr qsizetype markerShapes = 4;

// The marker of the kind at `style` in the kinds' order: tints first, then shapes.
QVariantMap markerOf(qsizetype style)
{
    const auto tint = static_cast<int>(style % QcWorldMapPreview::markerTints);
    const auto shape = static_cast<QcWorldMapPreview::MarkerShape>((style / QcWorldMapPreview::markerTints) % markerShapes);
    return {{u"style"_s, style}, {u"shape"_s, QVariant::fromValue(shape)}, {u"tint"_s, tint}};
}


} // namespace

QcWorldMapPreview::QcWorldMapPreview(std::function<QString()> language, QObject *parent)
    : QObject(parent)
    , m_language(std::move(language))
    , m_worker(std::make_shared<QcWorldMapWorker>())
    , m_pool(std::make_unique<QcWorkerPool>(1))
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(settleMs);
    connect(&m_settle, &QTimer::timeout, this, [this] { start(QcWorldMapRequest::Passes::All); });
}

// The running build stops at its next row; m_pool, destroyed first, drops queued builds and joins it.
QcWorldMapPreview::~QcWorldMapPreview()
{
    newBuild();
}

QString QcWorldMapPreview::source() const
{
    return stage() == Stage::Empty ? QString() : u"image://%1/%2"_s.arg(QLatin1StringView(QcWorldMapImageProvider::providerId)).arg(m_revision);
}

void QcWorldMapPreview::setSeedText(const QString &text)
{
    if (text == m_seedText) {
        return;
    }
    m_seedText = text;
    emit seedTextChanged();
    // Another world: its villages and structures lie elsewhere.
    setSiteList({});
    requestUpdate();
}

void QcWorldMapPreview::setActive(bool active)
{
    if (active == this->active()) {
        return;
    }
    m_active = active;
    emit activeChanged();
    if (!active) {
        // Hidden (a world starts, the menu moves on): the worker stops and lets go of the world;
        // showing the page again computes the map anew.
        stopAndForget();
        setBusy(false);
        return;
    }
    requestUpdate();
}

void QcWorldMapPreview::setShowSites(bool show)
{
    if (show == showSites()) {
        return;
    }
    m_showSites = show;
    emit showSitesChanged();
    if (!show) {
        // Only the markers go; sites of a build still running are dropped when they come.
        setSiteList({});
        return;
    }
    // Every kind shows again.
    setHiddenKinds({});
    if (mapIsCurrent()) {
        start(QcWorldMapRequest::Passes::SitesOnly);
        return;
    }
    requestUpdate();
}

void QcWorldMapPreview::zoomIn()
{
    const auto nearer = std::ranges::find_if(radii.rbegin(), radii.rend(), [this](int radius) { return radius < this->radius(); });
    if (nearer == radii.rend()) {
        qCWarning(lcPreview, "Map preview: cannot zoom in beyond %d blocks", radius());
        return;
    }
    QcBiomeMap::Area area = view();
    area.radius = *nearer;
    setView(area);
}

void QcWorldMapPreview::zoomOut()
{
    const auto farther = std::ranges::find_if(radii, [this](int radius) { return radius > this->radius(); });
    if (farther == radii.end()) {
        qCWarning(lcPreview, "Map preview: cannot zoom out beyond %d blocks", radius());
        return;
    }
    QcBiomeMap::Area area = view();
    area.radius = *farther;
    setView(area);
}

void QcWorldMapPreview::pan(double dx, double dz)
{
    if (!std::isfinite(dx) || !std::isfinite(dz)) {
        qCWarning(lcPreview, "Map preview: pan by %g, %g refused: not a number", dx, dz);
        return;
    }
    QcBiomeMap::Area area = view();
    const auto moved = [width = 2.0 * area.radius](int centre, double by) {
        return static_cast<int>(std::clamp(std::lround(centre + by * width), long{-maxCentre}, long{maxCentre}));
    };
    area.centreX = moved(area.centreX, dx);
    area.centreZ = moved(area.centreZ, dz);
    setView(area);
}

void QcWorldMapPreview::resetView()
{
    setView({0, 0, defaultRadius, 1});
}

void QcWorldMapPreview::setKindShown(const QString &kind, bool shown)
{
    if (!siteKinds().contains(kind)) {
        qCWarning(lcPreview, "Map preview: kind %s refused: unknown", qPrintable(kind));
        return;
    }
    QSet<QString> hidden = hiddenKinds();
    if (shown) {
        hidden.remove(kind);
    } else {
        hidden.insert(kind);
    }
    setHiddenKinds(std::move(hidden));
    updateMarkers();
}

void QcWorldMapPreview::setWorld(std::shared_ptr<const QcContentRegistry> content, const QJsonObject &values)
{
    if (content == this->content() && values == this->values()) {
        return;
    }
    setContent(std::move(content));
    setValues(values);
    setSiteList({});
    if (!this->content()) {
        clear();
        return;
    }
    requestUpdate();
}

void QcWorldMapPreview::setView(QcBiomeMap::Area view)
{
    // The same view (a zoom at its end, a pan by nothing) changes nothing.
    if (view == m_view) {
        return;
    }
    m_view = view;
    emit viewChanged();
    requestUpdate();
}

void QcWorldMapPreview::setBusy(bool busy)
{
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void QcWorldMapPreview::setPasses(int done, int count)
{
    if (done != m_passesDone || count != m_passCount) {
        m_passesDone = done;
        m_passCount = count;
        emit progressChanged();
    }
}

void QcWorldMapPreview::requestUpdate()
{
    if (!active() || !content()) {
        return;
    }
    // The running build is stale now: it stops at its next row and its results are dropped, so
    // the map does not jump back to an older view or seed while the changes go on.
    newBuild();
    setBusy(true);
    setPasses(0, mapPasses + (showSites() ? 1 : 0));
    m_settle.start();
}

bool QcWorldMapPreview::mapIsCurrent() const
{
    return active() && content() && stage() == Stage::Fine && !busy();
}

void QcWorldMapPreview::start(QcWorldMapRequest::Passes passes)
{
    if (!active() || !content()) {
        return;
    }

    // A new build number stops the running build at its next row; builds not started yet go.
    const quint64 build = newBuild();
    pool().discardQueued();
    setPassMilliseconds(QcWorldMapResult::Pass::Sites, -1);
    if (passes == QcWorldMapRequest::Passes::All) {
        setPassMilliseconds(QcWorldMapResult::Pass::Coarse, -1);
        setPassMilliseconds(QcWorldMapResult::Pass::Fine, -1);
    }
    setBusy(true);
    // Only the sites: the map's passes are there already.
    const int sitesPass = showSites() ? 1 : 0;
    setPasses(passes == QcWorldMapRequest::Passes::All ? 0 : mapPasses, mapPasses + sitesPass);

    const QcWorldMapRequest request{content(), values(), QcWorldLibrary::seedFromText(m_seedText), view(), showSites(), passes};
    pool().submit([this, worker = m_worker, request, build, hook = m_resultHook] {
        bool stopped = false;
        const auto cancelled = [this, build, &stopped] {
            stopped = stopped || this->build() != build;
            return stopped;
        };
        const auto deliver = [this, &hook](QcWorldMapResult result) {
            if (hook) {
                hook(result);
            }
            if (m_results.push(std::move(result))) {
                QMetaObject::invokeMethod(this, &QcWorldMapPreview::onResults, Qt::QueuedConnection);
            }
        };
        worker->run(request, build, cancelled, deliver);
        if (stopped) {
            ++m_cancelledRuns;
            return;
        }
        QcWorldMapResult done;
        done.build = build;
        done.pass = QcWorldMapResult::Pass::Done;
        deliver(std::move(done));
    });
}

void QcWorldMapPreview::onResults()
{
    for (QcWorldMapResult &result : m_results.takeAll()) {
        // Results of an older build (the inputs changed since) are dropped.
        if (result.build == build()) {
            apply(std::move(result));
        }
    }
}

void QcWorldMapPreview::apply(QcWorldMapResult result)
{
    if (result.pass == QcWorldMapResult::Pass::Done) {
        setBusy(false);
        return;
    }

    setPassMilliseconds(result.pass, result.milliseconds);
    setPasses(static_cast<int>(result.pass) + 1, passCount());
    if (result.pass == QcWorldMapResult::Pass::Sites) {
        // The switch was turned off while they were planned.
        if (showSites()) {
            setSiteKinds(std::move(result.siteKinds));
            setSiteList(std::move(result.sites));
        }
        return;
    }

    setStage(result.pass == QcWorldMapResult::Pass::Fine ? Stage::Fine : Stage::Coarse);
    setImage(result.image);
    setShown(std::move(result));
    retranslate();
}

void QcWorldMapPreview::setSiteList(QList<QcBiomeMap::Site> sites)
{
    m_siteList = std::move(sites);
    updateMarkers();
}

void QcWorldMapPreview::updateMarkers()
{
    // Placed on the shown image's square, which the markers move and zoom with.
    const QcBiomeMap::Area area = shown().area;
    const QcWorldMapTexts names = texts();
    QVariantList markers;
    if (area.radius > 0) {
        const double width = 2.0 * area.radius;
        for (const QcBiomeMap::Site &site : siteList()) {
            const double x = (site.x - (area.centreX - area.radius)) / width;
            const double y = (site.z - (area.centreZ - area.radius)) / width;
            if (x < 0.0 || x >= 1.0 || y < 0.0 || y >= 1.0) {
                continue;
            }
            if (hiddenKinds().contains(site.name)) {
                continue;
            }
            const SiteKind kind = site.kind == QcBiomeMap::Site::Kind::Village ? SiteKind::Village : SiteKind::Structure;
            const qsizetype style = siteKinds().indexOf(site.name);
            QVariantMap marker = markerOf(style < 0 ? siteKinds().size() : style);
            const QString title = names.kind(site.name);
            marker.insert(QVariantMap{{u"x"_s, x},
                                      {u"y"_s, y},
                                      {u"kind"_s, QVariant::fromValue(kind)},
                                      {u"name"_s, site.name},
                                      {u"title"_s, title},
                                      {u"text"_s, QcWorldMapTexts::place(title, site.x, site.z)}});
            markers.append(marker);
        }
    }
    setMarkers(std::move(markers), siteLegendOfList());
}

QVariantList QcWorldMapPreview::siteLegendOfList() const
{
    QHash<QString, int> counts;
    for (const QcBiomeMap::Site &site : siteList()) {
        ++counts[site.name];
    }
    QVariantList legend;
    for (qsizetype style = 0; style < siteKinds().size(); ++style) {
        const QString &kind = siteKinds().at(style);
        if (const int count = counts.value(kind); count > 0) {
            QVariantMap entry = markerOf(style);
            entry.insert(QVariantMap{{u"name"_s, kind}, {u"title"_s, texts().kind(kind)}, {u"count"_s, count}, {u"shown"_s, !hiddenKinds().contains(kind)}});
            legend.append(entry);
        }
    }
    return legend;
}


QVariantMap QcWorldMapPreview::placeAt(double x, double y) const
{
    const QcBiomeMap::Area area = shown().area;
    if (stage() == Stage::Empty || !(x >= 0.0 && x < 1.0 && y >= 0.0 && y < 1.0) || area.radius <= 0) {
        return {};
    }
    const auto blockX = static_cast<int>(std::floor(area.centreX - area.radius + x * 2.0 * area.radius));
    const auto blockZ = static_cast<int>(std::floor(area.centreZ - area.radius + y * 2.0 * area.radius));
    const auto cellX = static_cast<int>(x * area.cells());
    const auto cellZ = static_cast<int>(y * area.cells());
    const QString name = texts().cell(shown(), cellX, cellZ);
    return {{u"name"_s, name}, {u"x"_s, blockX}, {u"z"_s, blockZ}, {u"text"_s, QcWorldMapTexts::place(name, blockX, blockZ)}};
}

QString QcWorldMapPreview::centreText() const
{
    return placeAt(0.5, 0.5).value(u"text"_s).toString();
}

void QcWorldMapPreview::setImage(const QImage &image)
{
    m_image = image;
    if (m_provider) {
        m_provider->setImage(image);
    }
    ++m_revision;
}

void QcWorldMapPreview::setLegend(QVariantList legend)
{
    m_legend = std::move(legend);
    emit imageChanged();
}

void QcWorldMapPreview::setMarkers(QVariantList markers, QVariantList legend)
{
    // The same markers and legend (none before and after, a new build without them) change nothing.
    if (markers == m_sites && legend == m_siteLegend) {
        return;
    }
    m_sites = std::move(markers);
    m_siteLegend = std::move(legend);
    emit sitesChanged();
}

void QcWorldMapPreview::retranslate()
{
    setLegend(texts().legend(shown()));
    updateMarkers();
}


void QcWorldMapPreview::clear()
{
    stopAndForget();
    setBusy(false);
    setPasses(0, 0);
    setStage(Stage::Empty);
    setImage({});
    setShown({});
    setLegend({});
    setSiteList({});
}

void QcWorldMapPreview::stopAndForget()
{
    m_settle.stop();
    newBuild();
    pool().discardQueued();
    pool().submit([worker = m_worker] { worker->forget(); });
}

