#pragma once

#include "app/preview/QcWorldMapTexts.h"
#include "app/preview/QcWorldMapWork.h"
#include "common/core/QcMailbox.h"

#include <QImage>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

class QcWorkerPool;
class QcWorldMapImageProvider;

// The map preview of a new world (GameController.worldOptions.preview, decision E-921): a top-down
// map of biomes, heights, the sea and rivers from the seed and the world generation options, drawn
// from exactly the generator the world will get (QcWorldMapWorker). One worker thread computes it,
// a coarse pass first, then a fine one, then the villages and structures when they are wanted; every
// change (seed, options, view) stops the running build at once (it ends at its next row, its results
// are dropped) and starts a new one when changes settle. The GUI thread only swaps the finished image (served as
// image://worldmap/<revision>) and the legend. Computes only while `active` (the page is shown).
class QcWorldMapPreview : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldMapPreview)
    QML_UNCREATABLE("Provided by GameController.worldOptions.preview")
    // The seed text of the form ("" is not a seed: the form passes its random one).
    Q_PROPERTY(QString seedText READ seedText WRITE setSeedText NOTIFY seedTextChanged FINAL)
    // Whether the page with the preview is shown; nothing is computed otherwise.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    // Villages and structures on the map: off by default, so the map does not give them away.
    Q_PROPERTY(bool showSites READ showSites WRITE setShowSites NOTIFY showSitesChanged FINAL)
    Q_PROPERTY(QString source READ source NOTIFY imageChanged FINAL)
    Q_PROPERTY(Stage stage READ stage NOTIFY imageChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    // How far the running build is, 0 to 1 (decision E-930): the share of its passes (coarse,
    // fine and, with the sites, the sites) that have arrived. 0 from the moment a change makes a
    // new build until its coarse map arrives, also while an older image is still shown.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    // The view: half its width in blocks and its centre.
    Q_PROPERTY(int radius READ radius NOTIFY viewChanged FINAL)
    Q_PROPERTY(int centreX READ centreX NOTIFY viewChanged FINAL)
    Q_PROPERTY(int centreZ READ centreZ NOTIFY viewChanged FINAL)
    Q_PROPERTY(bool canZoomIn READ canZoomIn NOTIFY viewChanged FINAL)
    Q_PROPERTY(bool canZoomOut READ canZoomOut NOTIFY viewChanged FINAL)
    // The biomes and the sea on the map, most cells first: maps with name, colour, share (0..1) and
    // percent (rounded).
    Q_PROPERTY(QVariantList legend READ legend NOTIFY imageChanged FINAL)
    // The villages and structures on the shown map, of the kinds shown: maps with x and y (0..1
    // across the map), kind (SiteKind), name (the kind's), title (its name in the language) and
    // the kind's marker: style (its place in QcBiomeMap::siteKinds), shape (MarkerShape) and tint
    // (0 to markerTints − 1). They stay at their place while
    // the view moves until the new ones come.
    Q_PROPERTY(QVariantList sites READ sites NOTIFY sitesChanged FINAL)
    // The kinds of villages and structures found, in the order of their markers: maps with name,
    // title, style, shape, tint, count and shown (setKindShown).
    Q_PROPERTY(QVariantList siteLegend READ siteLegend NOTIFY sitesChanged FINAL)
    // What the centre of the shown map is, with its block column ("Plains, x 0, z 0"; empty without
    // a map): the line under the map while it has the keyboard's focus.
    Q_PROPERTY(QString centreText READ centreText NOTIFY imageChanged FINAL)
    // How far from the view's centre villages and structures are looked for, in blocks.
    Q_PROPERTY(int sitesRadius READ sitesRadius CONSTANT FINAL)

public:
    // What the map shows: nothing yet, the coarse pass (the fine one follows), the fine pass.
    enum class Stage { Empty, Coarse, Fine };
    Q_ENUM(Stage)
    enum class SiteKind { Village, Structure };
    Q_ENUM(SiteKind)
    // The shape of a kind's marker; with its tint (one of four colours) it tells the kinds apart.
    enum class MarkerShape { Square, Circle, Diamond, Ring };
    Q_ENUM(MarkerShape)
    static constexpr int markerTints = 4;

    // The view's half widths in blocks, from near to far.
    static constexpr std::array<int, 5> radii{256, 512, 1024, 2048, 4096};
    static constexpr int defaultRadius = 1024;
    // How long a change waits for the next one before the map is computed (a slider being dragged).
    static constexpr int settleMs = 120;

    // `language` gives the language of the content's texts.
    explicit QcWorldMapPreview(std::function<QString()> language, QObject *parent = nullptr);
    ~QcWorldMapPreview() override;
    Q_DISABLE_COPY_MOVE(QcWorldMapPreview)

    QString seedText() const { return m_seedText; }
    void setSeedText(const QString &text);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool showSites() const { return m_showSites; }
    void setShowSites(bool show);
    QString source() const;
    Stage stage() const { return m_stage; }
    bool busy() const { return m_busy; }
    double progress() const { return passCount() > 0 ? static_cast<double>(m_passesDone) / passCount() : 0.0; }
    int radius() const { return view().radius; }
    int centreX() const { return view().centreX; }
    int centreZ() const { return view().centreZ; }
    bool canZoomIn() const { return radius() > radii.front(); }
    bool canZoomOut() const { return radius() < radii.back(); }
    QVariantList legend() const { return m_legend; }
    QVariantList sites() const { return m_sites; }
    QVariantList siteLegend() const { return m_siteLegend; }
    int sitesRadius() const { return QcWorldMapWorker::sitesRadius; }

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    // Moves the view by fractions of its width (east and south positive), e.g. a drag or an arrow key.
    Q_INVOKABLE void pan(double dx, double dz);
    // Back to the world's origin at the default zoom.
    Q_INVOKABLE void resetView();
    // Shows or hides the markers of one kind of site (a name of siteLegend); only the display
    // changes. Switching showSites on shows every kind again.
    Q_INVOKABLE void setKindShown(const QString &kind, bool shown);
    // What the shown map has at (x, y), fractions of its width from the north-west corner: maps
    // with name (biome or sea, as in the legend), x and z (the block column) and text (all three);
    // empty outside the map or without one. Reads the cells of the shown image; computes nothing.
    Q_INVOKABLE QVariantMap placeAt(double x, double y) const;
    QString centreText() const;

    // The content and the form's option values the map is computed from (QcWorldOptionsController);
    // a null content (released) stops and empties the preview.
    void setWorld(std::shared_ptr<const QcContentRegistry> content, const QJsonObject &values);
    // The legend in the current language (after a change of the language).
    void retranslate();
    void setProvider(QcWorldMapImageProvider *provider) { m_provider = provider; }

    // For tests and the measurements: the shown image, how long the passes of the shown build took
    // (coarse, fine, sites; -1 not yet) and how many builds a change cancelled while they ran.
    const QImage &image() const { return m_image; }
    std::array<qint64, 3> passMilliseconds() const { return m_passMs; }
    int cancelledRuns() const { return m_cancelledRuns.load(); }
    // For tests: called on the worker thread with each result before it is handed over (it may
    // block there); taken by the builds started afterwards.
    using ResultHook = std::function<void(const QcWorldMapResult &)>;
    void setResultHook(ResultHook hook) { m_resultHook = std::move(hook); }

signals:
    void seedTextChanged();
    void activeChanged();
    void showSitesChanged();
    void imageChanged();
    void busyChanged();
    void progressChanged();
    void viewChanged();
    void sitesChanged();

private:
    const std::shared_ptr<const QcContentRegistry> &content() const { return m_content; }
    void setContent(std::shared_ptr<const QcContentRegistry> content) { m_content = std::move(content); }
    const QJsonObject &values() const { return m_values; }
    void setValues(const QJsonObject &values) { m_values = values; }
    void setStage(Stage stage) { m_stage = stage; }
    // The shown map, handed to the image provider under a new revision; a null image for none.
    void setImage(const QImage &image);
    const QcWorldMapResult &shown() const { return m_shown; }
    void setShown(QcWorldMapResult shown) { m_shown = std::move(shown); }
    void setLegend(QVariantList legend);
    // The markers and the legend of the kinds (sites, siteLegend).
    void setMarkers(QVariantList markers, QVariantList legend);
    // The villages and structures found for the current world, in world columns, and every kind
    // the generator can place.
    const QList<QcBiomeMap::Site> &siteList() const { return m_siteList; }
    void setSiteList(QList<QcBiomeMap::Site> sites);
    const QStringList &siteKinds() const { return m_siteKinds; }
    void setSiteKinds(QStringList kinds) { m_siteKinds = std::move(kinds); }
    const QSet<QString> &hiddenKinds() const { return m_hiddenKinds; }
    void setHiddenKinds(QSet<QString> kinds) { m_hiddenKinds = std::move(kinds); }
    // The legend of the kinds in siteList().
    QVariantList siteLegendOfList() const;
    // The names of the biomes and kinds in the current language.
    QcWorldMapTexts texts() const { return {content().get(), m_language()}; }
    // The markers of siteList() on the shown map.
    void updateMarkers();
    // Whether the shown map is the finished one of the current inputs (only the sites may be missing).
    bool mapIsCurrent() const;
    // The newest build's number, and a new one (which stops the running build).
    quint64 build() const { return m_build.load(); }
    quint64 newBuild() { return ++m_build; }
    void setPassMilliseconds(QcWorldMapResult::Pass pass, qint64 milliseconds) { m_passMs.at(static_cast<std::size_t>(pass)) = milliseconds; }
    QcWorkerPool &pool() const { return *m_pool; }
    const QcBiomeMap::Area &view() const { return m_view; }
    void setView(QcBiomeMap::Area view);
    void setBusy(bool busy);
    // The passes of the running build that have arrived, of how many it has.
    int passCount() const { return m_passCount; }
    void setPasses(int done, int count);
    // A change: the running build stops now, a new one starts once changes settle.
    void requestUpdate();
    // Starts a build for the current inputs (dropping the running one).
    void start(QcWorldMapRequest::Passes passes);
    void onResults();
    void apply(QcWorldMapResult result);
    void clear();
    // Stops the running build, drops the queued ones and lets the worker forget the world.
    void stopAndForget();

    std::function<QString()> m_language;
    std::shared_ptr<const QcContentRegistry> m_content;
    QJsonObject m_values;
    QString m_seedText;
    bool m_active = false;
    bool m_showSites = false;
    QcBiomeMap::Area m_view{0, 0, defaultRadius, 1};
    Stage m_stage = Stage::Empty;
    bool m_busy = false;
    int m_passesDone = 0;
    int m_passCount = 0;
    QImage m_image;
    // The last coarse or fine result, for the legend in another language.
    QcWorldMapResult m_shown;
    quint64 m_revision = 0;
    QVariantList m_legend;
    QVariantList m_sites;
    QVariantList m_siteLegend;
    QStringList m_siteKinds;
    QSet<QString> m_hiddenKinds;
    QList<QcBiomeMap::Site> m_siteList;
    ResultHook m_resultHook; // NOLINT(qc-member-access): a test hook, set by its setter and read only in start()
    std::array<qint64, 3> m_passMs{-1, -1, -1};
    QcWorldMapImageProvider *m_provider = nullptr;
    QTimer m_settle; // NOLINT(qc-member-access): a timer, started and stopped through its own methods
    // The number of the newest build; a running build stops when it is no longer the newest. Read
    // by the worker, hence atomic.
    std::atomic<quint64> m_build{0};
    std::atomic<int> m_cancelledRuns{0};
    // Results from the worker; it hands them over under a std::mutex and only wakes this object with a
    // queued call (decision E-231).
    QcMailbox<QcWorldMapResult> m_results;
    // Used only by jobs on the worker thread (one at a time): it keeps the last generator.
    std::shared_ptr<QcWorldMapWorker> m_worker;
    // One worker thread. Declared last: destroyed first, it drops queued builds and joins a running
    // one (cancelled by the destructor) before the mailbox and this object go away.
    std::unique_ptr<QcWorkerPool> m_pool;
};
