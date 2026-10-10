#include "QcBenchmarkRun.h"

#include "QcClientSession.h"
#include "QcGameController.h"
#include "QcGameInput.h"
#include "QcLocalServerHost.h"
#include "QcMeshScheduler.h"
#include "QcProcessMemory.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>


using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcGame, "qtcraft.game")

// Mean and longest time per server tick phase (decision E-225), "key=value" pairs for the BENCH line.
QString tickPhasePairs(const QcTickStats::Summary &ticks)
{
    QStringList pairs;
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        const QLatin1StringView id(QcTickPhases::ids[std::size_t(phase)]);
        pairs.append(u"tick_%1_avg_ms=%2"_s.arg(id).arg(ticks.phaseAverageMs[std::size_t(phase)], 0, 'f', 4));
        pairs.append(u"tick_%1_max_ms=%2"_s.arg(id).arg(ticks.phaseWorstMs[std::size_t(phase)], 0, 'f', 3));
    }
    return pairs.join(u' ');
}

} // namespace

QcBenchmarkRun::QcBenchmarkRun(QcGameController &game, const QcSessionParts &parts, QcGameInput &input,
                               const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_game(game)
    , m_parts(parts)
    , m_input(input)
    , m_settings(settings)
{
}

void QcBenchmarkRun::configure(int seconds, const QString &scenario)
{
    m_seconds = seconds;
    m_walk = scenario != u"stand" && scenario != u"light" && scenario != u"liquids";
    m_sprint = scenario != u"walk";
    m_light = scenario == u"light";
    m_liquids = scenario == u"liquids";
}

void QcBenchmarkRun::scheduleStart() const
{
    if (m_seconds > 0) {
        QTimer::singleShot(3000, this, &QcBenchmarkRun::start);
    }
}

void QcBenchmarkRun::recordFrame()
{
    if (!m_running) {
        return;
    }

    const qint64 ns = m_frame.nsecsElapsed();
    m_frameNs.append(ns);
    if (ns > 100'000'000) {
        // A hidden or covered window may stall frames for reasons outside the game.
        const QQuickWindow *window = m_parts.window;
        qCInfo(lcGame) << "Benchmark: frame" << m_frameNs.size() << "took" << ns / 1'000'000 << "ms, window"
                       << (window && window->isExposed() ? "exposed" : "not exposed") << "visibility"
                       << (window ? window->visibility() : QWindow::Hidden) << "active" << (window && window->isActive());
    }
    m_frame.restart();
}

void QcBenchmarkRun::recordGuiFrame(double guiCpuMs, qint64 entityNs, qint64 previewNs, const QList<QcRenderSample> &samples)
{
    if (!m_running) {
        return;
    }

    m_frameProfile.addGuiFrame(guiCpuMs, double(entityNs) / 1e6, double(previewNs) / 1e6);
    m_frameProfile.addRenderSamples(samples);
}

void QcBenchmarkRun::start()
{
    if (!m_parts.session) {
        return;
    }

    resetMeasurement();
    if (m_walk) {
        startWalking();
    } else {
        startStanding();
    }
}

void QcBenchmarkRun::resetMeasurement()
{
    m_frameNs.clear();
    m_frameProfile.clear();
    if (m_parts.server) {
        m_parts.server->resetTickTimes();
    }
    m_parts.meshes->resetStats();
}

void QcBenchmarkRun::startStanding()
{
    m_frame.start();
    m_running = true;
    QTimer::singleShot(m_seconds * 1000, this, &QcBenchmarkRun::finish);
    if (m_light) {
        startTorchRing();
    }
    if (m_liquids) {
        startLiquidRing();
    }
}

void QcBenchmarkRun::startTorchRing()
{
    auto *torches = new QTimer(this);
    auto step = std::make_shared<int>(0);
    connect(torches, &QTimer::timeout, this, [this, torches, step] {
        if (!m_running || !m_parts.session) {
            torches->deleteLater();
            return;
        }

        constexpr int ring = 12;
        const int index = (*step / 2) % ring;
        const double angle = index * 2.0 * std::numbers::pi / ring;
        const int x = int(std::lround(std::cos(angle) * 9.0));
        const int z = int(std::lround(std::sin(angle) * 9.0));
        m_parts.session->sendChat(u"/setblock ~%1 ~ ~%2 %3"_s.arg(x).arg(z).arg(*step % 2 == 0 ? u"torch"_s : u"air"_s));
        ++*step;
    });
    torches->start(250);
}

void QcBenchmarkRun::startLiquidRing()
{
    auto *sources = new QTimer(this);
    auto step = std::make_shared<int>(0);
    connect(sources, &QTimer::timeout, this, [this, sources, step] {
        if (!m_running || !m_parts.session) {
            sources->deleteLater();
            return;
        }

        constexpr int ring = 6;
        const int index = (*step / 2) % ring;
        const double angle = index * 2.0 * std::numbers::pi / ring;
        const int x = int(std::lround(std::cos(angle) * 8.0));
        const int z = int(std::lround(std::sin(angle) * 8.0));
        const QString source = index % 3 == 2 ? u"lava"_s : u"water"_s;
        m_parts.session->sendChat(u"/setblock ~%1 ~4 ~%2 %3"_s.arg(x).arg(z).arg(*step % 2 == 0 ? source : u"air"_s));
        ++*step;
    });
    sources->start(500);
}

void QcBenchmarkRun::startWalking()
{
    m_game.setInputEnabled(true);
    using A = QcKeyBindings::Action;
    const QcKeyBindings &keys = m_settings.keys()->bindings();
    QSet<int> held{keys.key(A::Forward, 0)};
    if (m_sprint) {
        held.insert(keys.key(A::Sprint, 0));
        held.insert(keys.key(A::Jump, 0));
    }
    m_input.holdKeys(std::move(held));
    m_frameNs.reserve(qsizetype(m_seconds) * 240);
    m_frame.start();
    m_running = true;
    m_checkpoint = m_parts.session->playerMotion().position;
    QTimer::singleShot(m_seconds * 1000, this, &QcBenchmarkRun::finish);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, timer] { steer(timer); });
    timer->start(1500);
}

void QcBenchmarkRun::steer(QTimer *timer)
{
    if (!m_running || !m_parts.session) {
        timer->deleteLater();
        return;
    }

    const QcVec3 position = m_parts.session->playerMotion().position;
    const QcVec3 moved = position - m_checkpoint;
    if (std::hypot(moved.x, moved.z) < 4.0) {
        QcPlayerInput intent = m_parts.session->intent();
        intent.yaw = std::fmod(intent.yaw + 67.0f, 360.0f);
        m_parts.session->setIntent(intent);
    }
    m_checkpoint = position;
}

void QcBenchmarkRun::finish()
{
    m_running = false;
    m_input.clearKeys();
    QList<qint64> sorted = m_frameNs;
    std::ranges::sort(sorted);
    if (sorted.isEmpty()) {
        qCWarning(lcGame) << "Benchmark: no frames rendered";
        QCoreApplication::exit(1);
        return;
    }

    const double p99Ms = double(sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)]) / 1e6;
    logBenchLine(sorted, p99Ms);
    logReport(sorted, p99Ms);
    QCoreApplication::quit();
}

void QcBenchmarkRun::logBenchLine(const QList<qint64> &sorted, double p99Ms) const
{
    qint64 total = 0;
    for (const qint64 ns : sorted) {
        total += ns;
    }

    const QcTickStats::Summary ticks = m_parts.server ? m_parts.server->tickStats() : QcTickStats::Summary();
    const double averageMs = double(total) / double(sorted.size()) / 1e6;
    const qsizetype entities = m_parts.session ? m_parts.session->entities().size() : 0;

    // Block changes until their sections show (with their light), the most sections and light jobs
    // waiting at once, and the light thread's work per job.
    const QcMeshScheduler::Stats mesh = m_parts.meshes->stats();
    const double latencyMs = mesh.latencySamples > 0 ? mesh.latencyTotalMs / double(mesh.latencySamples) : 0.0;
    const double lightMs = mesh.lightJobs > 0 ? mesh.lightTotalMs / double(mesh.lightJobs) : 0.0;

    qCInfo(lcGame).noquote() << u"BENCH frame_avg_ms=%1 frame_p99_ms=%2 tick_avg_ms=%3 tick_worst_ms=%4 peak_rss_mb=%5 entities=%6 "
                                u"change_mesh_avg_ms=%7 change_mesh_max_ms=%8 change_sections=%9 mesh_backlog_max=%10 "
                                u"light_jobs=%11 light_job_avg_ms=%12 light_job_max_ms=%13 mesh_jobs=%14 mesh_job_avg_ms=%15 mesh_job_max_ms=%16 "
                                u"tick_late_avg_ms=%17 tick_late_p99_ms=%18 tick_late_max_ms=%19 ticks=%20 ticks_skipped=%21 %22 %23"_s
                                    .arg(averageMs, 0, 'f', 3)
                                    .arg(p99Ms, 0, 'f', 3)
                                    .arg(ticks.averageMs, 0, 'f', 3)
                                    .arg(ticks.worstMs, 0, 'f', 3)
                                    .arg(QcProcessMemory::peakMb(), 0, 'f', 1)
                                    .arg(entities)
                                    .arg(latencyMs, 0, 'f', 3)
                                    .arg(mesh.latencyWorstMs, 0, 'f', 3)
                                    .arg(mesh.latencySamples)
                                    .arg(mesh.worstBacklog)
                                    .arg(mesh.lightJobs)
                                    .arg(lightMs, 0, 'f', 3)
                                    .arg(mesh.lightWorstMs, 0, 'f', 3)
                                    .arg(mesh.meshJobs)
                                    .arg(mesh.meshJobs > 0 ? mesh.meshTotalMs / double(mesh.meshJobs) : 0.0, 0, 'f', 3)
                                    .arg(mesh.meshWorstMs, 0, 'f', 3)
                                    .arg(ticks.lateAverageMs, 0, 'f', 3)
                                    .arg(ticks.lateP99Ms, 0, 'f', 3)
                                    .arg(ticks.lateMaxMs, 0, 'f', 3)
                                    .arg(ticks.ticks)
                                    .arg(ticks.skipped)
                                    .arg(m_frameProfile.summary(), tickPhasePairs(ticks));
}

void QcBenchmarkRun::logReport(const QList<qint64> &sorted, double p99Ms) const
{
    qint64 total = 0;
    for (const qint64 ns : sorted) {
        total += ns;
    }

    const double averageFps = double(sorted.size()) * 1e9 / double(total);
    const double worstMs = double(sorted.constLast()) / 1e6;
    const QcVec3 position = m_parts.session ? m_parts.session->playerMotion().position : QcVec3{};
    const QString report = u"Benchmark: %1 frames in %2 s, average %3 FPS, 99th percentile frame %4 ms (%5 FPS), worst %6 ms, "
                           u"view distance %7, walked to %8 / %9, %10 chunks loaded"_s.arg(sorted.size())
                               .arg(m_seconds)
                               .arg(averageFps, 0, 'f', 1)
                               .arg(p99Ms, 0, 'f', 2)
                               .arg(1000.0 / p99Ms, 0, 'f', 1)
                               .arg(worstMs, 0, 'f', 2)
                               .arg(m_settings.viewDistance())
                               .arg(position.x, 0, 'f', 0)
                               .arg(position.z, 0, 'f', 0)
                               .arg(m_parts.session && m_parts.session->world() ? m_parts.session->world()->chunkCount() : 0);

    qCInfo(lcGame).noquote() << report;
}
