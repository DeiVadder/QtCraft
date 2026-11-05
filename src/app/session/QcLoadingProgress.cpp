// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/QcLoadingProgress.h"

#include "app/debug/QcAppLogging.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientWorld.h"

#include <QMetaEnum>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

using Stage = QcLoadingProgress::Stage;

// The share of the whole start at which each stage begins; the chunk and mesh stages fill their
// share by their counts. Content and world take little time next to the terrain (decision E-943).
constexpr std::array<double, 7> stageStart{0.0, 0.0, 0.08, 0.15, 0.2, 0.7, 1.0};

constexpr std::size_t indexOf(Stage stage)
{
    return static_cast<std::size_t>(stage);
}

// Every chunk next to `pos` has arrived with its light and waits for no mesh (so the sections of
// `pos` were marked again once it came), or never comes because it lies beyond the bound of the
// world.
bool neighboursSettled(const QcMeshScheduler &meshes, QcChunkPos pos)
{
    static constexpr std::array<std::array<int, 2>, 4> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    return std::ranges::all_of(sides, [&meshes, pos](std::array<int, 2> side) {
        const QcChunkPos next{pos.x + side[0], pos.z + side[1]};
        return !next.isInsideWorld() || meshes.chunkMeshed(next);
    });
}

// The value of the enum `T` named `name` (case-insensitive), or nullopt.
template <typename T>
std::optional<int> enumValue(QStringView name)
{
    const QMetaEnum values = QMetaEnum::fromType<T>();
    for (int index = 0; index < values.keyCount(); ++index) {
        if (name.compare(QLatin1StringView(values.key(index)), Qt::CaseInsensitive) == 0) {
            return values.value(index);
        }
    }
    return std::nullopt;
}

} // namespace

QcLoadingProgress::QcLoadingProgress(QObject *parent)
    : QObject(parent)
{
}

double QcLoadingProgress::fraction() const
{
    const Stage current = stage();
    const double start = stageStart[indexOf(current)];
    if ((current != Stage::Chunks && current != Stage::Meshes) || chunksNeeded() <= 0) {
        return start;
    }

    const double done = current == Stage::Chunks ? chunksArrived() : chunksMeshed();
    const double share = stageStart[indexOf(current) + 1] - start;
    return start + share * std::clamp(done / chunksNeeded(), 0.0, 1.0);
}

void QcLoadingProgress::begin(Kind kind)
{
    m_kind = kind;
    setShownOnly(false);
    setArea({});
    for (std::size_t index = 0; index < indexOf(Stage::Ready) + 1; ++index) {
        setStageEndMs(index, -1);
    }
    m_sinceBegin.start();
    m_sincePlaying.invalidate();
    setStage(kind == Kind::Network ? Stage::Joining : Stage::Content);
    emit changed();
}

void QcLoadingProgress::advance(Stage next)
{
    const bool follows = (next == Stage::World && stage() == Stage::Content) || (next == Stage::Joining && stage() == Stage::World);
    if (!follows) {
        qCWarning(lcGame) << "Loading progress: refused the step to" << next << "in" << stage() << "(only World after Content, Joining after World)";
        return;
    }

    setStage(next);
    emit changed();
}

void QcLoadingProgress::observe(Area area, bool sessionPlaying)
{
    // The area is only known once the server placed the player (the session plays); a state the
    // test hook shows stays as it is.
    if (shownOnly() || stage() < Stage::Joining || stage() == Stage::Ready || !sessionPlaying) {
        return;
    }

    notePlaying();
    setArea(area);
    Stage next = area.arrived < area.needed ? Stage::Chunks : area.meshed < area.needed ? Stage::Meshes : Stage::Ready;
    if (next != Stage::Ready && sincePlaying().elapsed() >= m_maxWaitMs) {
        qCWarning(lcGame).nospace() << "Loading progress: the spawn area is not meshed after " << m_maxWaitMs << " ms (chunks " << area.arrived
                                    << " arrived, " << area.meshed << " meshed of " << area.needed << "); the world shows anyway";
        next = Stage::Ready;
    }

    setStage(std::max(next, stage()));
    emit changed();
    if (stage() == Stage::Ready) {
        logTimes();
        emit ready();
    }
}

void QcLoadingProgress::reset()
{
    if (stage() == Stage::Idle) {
        return;
    }
    setShownOnly(false);
    setArea({});
    setStage(Stage::Idle);
    emit changed();
}

void QcLoadingProgress::show(const Shown &shown)
{
    m_kind = shown.kind;
    setShownOnly(true);
    setArea(shown.area);
    setStage(shown.stage);
    emit changed();
}

void QcLoadingProgress::setStage(Stage stage)
{
    if (stage == m_stage) {
        return;
    }
    // Every stage passed on the way ends now.
    if (sinceBegin().isValid()) {
        const qint64 now = sinceBegin().elapsed();
        for (std::size_t index = indexOf(m_stage); index < indexOf(stage); ++index) {
            setStageEndMs(index, now);
        }
    }
    m_stage = stage;
}

void QcLoadingProgress::logTimes() const
{
    QStringList parts;
    qint64 previous = 0;
    const QMetaEnum stages = QMetaEnum::fromType<Stage>();
    for (std::size_t index = indexOf(Stage::Content); index < indexOf(Stage::Ready); ++index) {
        const qint64 end = stageEndMs(index);
        if (end < 0) {
            continue;
        }
        parts.append(u"%1 %2"_s.arg(QString::fromLatin1(stages.valueToKey(static_cast<int>(index))).toLower()).arg(end - previous));
        previous = end;
    }
    qCInfo(lcGame).noquote() << u"World start: playable after %1 ms (%2 ms; spawn area %3 chunks)"_s.arg(previous).arg(parts.join(u", "_s)).arg(chunksNeeded());
}

QcLoadingProgress::Area QcLoadingProgress::measure(const QcClientWorld &world, const QcMeshScheduler &meshes, QcChunkPos centre, int radius)
{
    Area area;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const QcChunkPos pos{centre.x + dx, centre.z + dz};
            if (!pos.isInsideWorld()) {
                continue;
            }
            ++area.needed;
            if (!world.chunk(pos)) {
                continue;
            }
            ++area.arrived;
            if (meshes.chunkMeshed(pos) && neighboursSettled(meshes, pos)) {
                ++area.meshed;
            }
        }
    }
    return area;
}

std::optional<QcLoadingProgress::Shown> QcLoadingProgress::parseShow(QStringView spec)
{
    const QList<QStringView> parts = spec.split(u',');
    const std::optional<int> stage = enumValue<Stage>(parts.value(0));
    const std::optional<int> kind = parts.size() > 1 ? enumValue<Kind>(parts.at(1)) : std::optional<int>{static_cast<int>(Kind::NewWorld)};
    if (!stage || !kind || (parts.size() != 1 && parts.size() != 2 && parts.size() != 5)) {
        return std::nullopt;
    }
    // Idle and Ready show no loading screen.
    if (*stage == static_cast<int>(Stage::Idle) || *stage == static_cast<int>(Stage::Ready)) {
        return std::nullopt;
    }

    std::array<int, 3> counts{};
    for (qsizetype index = 2; index < parts.size(); ++index) {
        bool ok = false;
        const int count = parts.at(index).toInt(&ok);
        if (!ok || count < 0) {
            return std::nullopt;
        }
        counts.at(static_cast<std::size_t>(index - 2)) = count;
    }
    // arrived, meshed, needed: never more meshed than arrived, nor more arrived than needed.
    if (counts[1] > counts[0] || counts[0] > counts[2]) {
        return std::nullopt;
    }
    return Shown{.stage = static_cast<Stage>(*stage), .kind = static_cast<Kind>(*kind), .area = {.needed = counts[2], .arrived = counts[0], .meshed = counts[1]}};
}
