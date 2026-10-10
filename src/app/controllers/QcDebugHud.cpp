#include "QcDebugHud.h"

#include "QcClientSession.h"
#include "QcDebugText.h"
#include "QcLabelProjector.h"
#include "QcLightProbe.h"
#include "QcMeshScheduler.h"
#include "QcPhysics.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"

#include <QFontDatabase>

using namespace Qt::StringLiterals;

namespace {

QList<qreal> toReals(const QList<float> &values)
{
    QList<qreal> reals;
    reals.reserve(values.size());
    for (const float value : values) {
        reals.append(qreal(value));
    }
    return reals;
}

} // namespace

QcDebugHud::QcDebugHud(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
    , m_visuals(parts)
    , m_lightLabels(new QcWorldLabelModel(this))
{
    connect(settings.debug(), &QcDebugSettings::levelChanged, this, [this] {
        resetClock();
        syncSubscription();
        emit levelChanged();
    });
    connect(settings.debug(), &QcDebugSettings::visualsChanged, this, &QcDebugHud::syncSubscription);
}

void QcDebugHud::updateWorld(double alpha, const QcVec3 &feet)
{
    m_visuals.update(m_settings.debug()->options(), alpha, feet);
}

void QcDebugHud::meshed(quint64 sectionKey)
{
    m_visuals.meshed(sectionKey, m_settings.debug()->options());
}

int QcDebugHud::level() const
{
    return m_settings.debug()->level();
}

QFont QcDebugHud::monoFont() const
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(12);
    return font;
}

void QcDebugHud::setStatsLogging(bool on)
{
    m_statsLogging = on;
    syncSubscription();
}

void QcDebugHud::syncSubscription() const
{
    if (!m_parts.session) {
        return;
    }

    // The compact display shows nothing of the server: no subscription, no messages.
    const QcDebugSettings &debug = *m_settings.debug();
    const bool stats = m_statsLogging || debug.levelValue() == QcDebugOptions::Level::Full;
    const quint8 flags = (stats ? QcMsg::DebugSubscribe::stats : 0) | (debug.mobGoals() ? QcMsg::DebugSubscribe::all : 0);
    m_parts.session->setDebugSubscription(flags);
}

QList<QcRenderSample> QcDebugHud::recordFrame(qint64 entityNs, bool benchmarkRunning)
{
    using Depth = QcFrameMonitor::Depth;
    const QcDebugOptions::Level shown = m_settings.debug()->levelValue();
    const Depth depth = benchmarkRunning || m_statsLogging || shown == QcDebugOptions::Level::Full ? Depth::Full
                        : shown == QcDebugOptions::Level::Compact                                    ? Depth::Interval
                                                                                                      : Depth::None;
    if (depth != m_frames.depth()) {
        m_frames.setDepth(depth, m_parts.view);
    }
    return m_frames.recordFrame(m_parts.view, entityNs);
}

void QcDebugHud::update(const QcVec3 &feet)
{
    const bool lights = m_settings.debug()->lightValues();
    const bool shown = m_settings.debug()->levelValue() != QcDebugOptions::Level::Off;
    if (!shown && !lights) {
        m_lightLabels->clear();
        return;
    }

    if (!m_textClock.isValid() || m_textClock.elapsed() >= textIntervalMs) {
        m_textClock.start();
        if (shown) {
            updateText(feet);
        }
        m_lightCells.clear();
        if (const std::optional<QcRaycastHit> &target = m_parts.session->target(); lights && target) {
            const QcFaceOffset offset = qcFaceOffset(target->face);
            m_lightCells = lightCells(target->block.offset(offset.dx, offset.dy, offset.dz));
        }
        // New values on the same cells: rebuilt rows, not just moved ones.
        m_lightLabels->clear();
    }
    updateLightLabels();
}

QcDebugSnapshot QcDebugHud::snapshot(const QcVec3 &feet, QcFrameMonitor::Reader reader)
{
    QcDebugSnapshot snapshot = QcDebugSnapshot::collect(*m_parts.session, feet);
    if (m_frames.depth() != QcFrameMonitor::Depth::None) {
        snapshot.frame = m_frames.frame(*m_parts.meshes, m_parts.view ? m_parts.view->meshCount() : 0, reader);
    }
    return snapshot;
}

void QcDebugHud::updateText(const QcVec3 &feet)
{
    QString general;
    QString timings;
    if (m_settings.debug()->levelValue() == QcDebugOptions::Level::Full) {
        const QcDebugSnapshot data = snapshot(feet, QcFrameMonitor::Reader::Display);
        general = QcDebugText::general(data).join(u'\n');
        timings = QcDebugText::timings(data).join(u'\n');
        m_frameGraph = toReals(m_frames.intervalHistory());
        m_tickGraph = toReals(m_parts.session->serverDebug().tickHistory());
    } else {
        QcDebugSnapshot data;
        data.frame = m_frames.frame(*m_parts.meshes, 0, QcFrameMonitor::Reader::Display);
        general = QcDebugText::compact(data);
        m_frameGraph.clear();
        m_tickGraph.clear();
    }

    if (general != m_generalText || timings != m_timingsText || !m_frameGraph.isEmpty()) {
        m_generalText = general;
        m_timingsText = timings;
        emit textChanged();
    }
}

QList<QcWorldLabel> QcDebugHud::lightCells(QcBlockPos centre) const
{
    const QcClientWorld *world = m_parts.session->world();
    if (!world) {
        return {};
    }

    const QcLightProbe probe(*world, centre, lightLabelRadius);
    constexpr int side = 2 * lightLabelRadius + 1;
    QList<QcWorldLabel> cells;
    for (int index = 0; index < side * side * side; ++index) {
        const QcBlockPos cell = centre.offset(index % side - lightLabelRadius, index / (side * side) - lightLabelRadius,
                                              index / side % side - lightLabelRadius);
        const std::optional<QcBlockId> below = world->blockAt(cell.offset(0, -1, 0));
        // Air resting on a block: where creatures spawn and players stand.
        if (world->blockAt(cell) != qcAirBlockId || !below || *below == qcAirBlockId) {
            continue;
        }
        const QcLightProbe::Levels light = probe.at(cell);
        cells.append({{cell.x + 0.5, cell.y + 0.15, cell.z + 0.5}, tr("S%1 B%2").arg(light.sky).arg(light.block), {}, false});
    }
    return cells;
}

void QcDebugHud::updateLightLabels()
{
    if (m_lightCells.isEmpty() || !m_parts.view || !m_parts.session->content()) {
        m_lightLabels->clear();
        return;
    }

    QHash<QcChunkPos, QList<QcWorldLabel>> byChunk;
    for (const QcWorldLabel &cell : std::as_const(m_lightCells)) {
        byChunk[QcPhysics::blockContaining(cell.position).chunk()].append(cell);
    }
    const QcLabelProjector::View view{m_parts.view->camera(), m_parts.view->width(), m_parts.view->height()};
    m_lightLabels->update(QcLabelProjector::visible(byChunk, view, m_parts.session->world()), *m_parts.session->content(),
                          m_settings.contentLanguage());
}
