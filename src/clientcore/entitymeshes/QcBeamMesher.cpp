// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcBeamMesher.h"

#include <array>
#include <cmath>
#include <numbers>

namespace {

// A column's cross-section: its axis and its four corners about it, as x and z offsets.
struct Ring
{
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    std::array<std::pair<double, double>, 4> corners{};
};

// The textures and strength of a column's faces: the atlas tile and how much of each section's
// colour they show.
struct Look
{
    QRectF tile;
    double strength = 1.0;
};

// A piece of a column from `bottom` to `top`, showing the part from `from` to `to` (0 to 1) of
// its texture tile along its height.
struct Span
{
    double bottom = 0.0;
    double top = 0.0;
    double from = 0.0;
    double to = 1.0;
};

Ring ringOf(const QcMsg::BeaconBeam &beam, double halfWidth, double angle)
{
    Ring ring{beam.pos.x + 0.5, beam.pos.z + 0.5, halfWidth * std::numbers::sqrt2, {}};
    for (std::size_t corner = 0; corner < ring.corners.size(); ++corner) {
        const double at = angle + std::numbers::pi / 4.0 + static_cast<double>(corner) * std::numbers::pi / 2.0;
        ring.corners[corner] = {ring.radius * std::cos(at), ring.radius * std::sin(at)};
    }
    return ring;
}

bool isInside(const Ring &ring, const QcVec3 &camera)
{
    return std::hypot(camera.x - ring.x, camera.z - ring.z) < ring.radius + QcBeamMesher::cameraClearance;
}

// The segments of the texture between `bottom` and `top`, each a whole tile long but the first and
// the last, moved up by `phase` blocks.
QList<Span> segmentsOf(double bottom, double top, double phase)
{
    constexpr double length = QcBeamMesher::segmentLength;
    QList<Span> spans;
    const auto first = static_cast<qint64>(std::floor((bottom - phase) / length));
    const auto end = static_cast<qint64>(std::ceil((top - phase) / length));
    for (qint64 step = first; step < end; ++step) {
        const double start = phase + static_cast<double>(step) * length;
        const double low = std::max(start, bottom);
        const double high = std::min(start + length, top);
        if (high > low) {
            spans.append({low, high, (low - start) / length, (high - start) / length});
        }
    }
    return spans;
}

// The colour 0xRRGGBB as a tint, at `strength` (the glow adds only a part of it).
std::array<quint8, 4> tintOf(quint32 colour, double strength)
{
    const auto channel = [strength](quint32 value) { return static_cast<quint8>(std::lround(static_cast<double>(value & 0xFF) * strength)); };
    return {channel(colour >> 16), channel(colour >> 8), channel(colour), 255};
}

// The four faces of `ring` along `span`, looking out, glowing by themselves.
void addRing(QcEntityMesh &mesh, const Ring &ring, const Span &span, std::array<quint8, 4> tint, const QRectF &tile)
{
    const auto u = [&tile](double along) { return static_cast<float>(tile.left() + along * tile.width()); };
    for (std::size_t side = 0; side < ring.corners.size(); ++side) {
        const auto [x0, z0] = ring.corners[side];
        const auto [x1, z1] = ring.corners[(side + 1) % ring.corners.size()];
        // From the next corner to this one, then up: counter-clockwise seen from outside.
        const std::array<QcVec3, 4> at{QcVec3{ring.x + x1, span.bottom, ring.z + z1}, QcVec3{ring.x + x0, span.bottom, ring.z + z0},
                                       QcVec3{ring.x + x0, span.top, ring.z + z0}, QcVec3{ring.x + x1, span.top, ring.z + z1}};
        std::array<QcEntityVertex, 4> quad;
        for (std::size_t corner = 0; corner < quad.size(); ++corner) {
            QcEntityVertex vertex = mesh.vertexAt(at[corner]);
            // The texture's bands (along its v) run up the beam: u along it, v across.
            vertex.u = corner < 2 ? u(span.from) : u(span.to);
            vertex.v = static_cast<float>(corner == 0 || corner == 3 ? tile.top() : tile.bottom());
            vertex.skyLight = 0.0f;
            vertex.blockLight = QcEntityVertex::emissiveLight;
            vertex.tint = tint;
            quad[corner] = vertex;
        }
        mesh.addQuad(quad);
    }
}

// The column of `ring` along every section of `beam`, its texture moved up by `phase` blocks.
void appendColumn(QcEntityMesh &mesh, const QcMsg::BeaconBeam &beam, const Ring &ring, double phase, const Look &look)
{
    // The beam starts in the beacon, whose cell is the first block of the first section.
    auto bottom = static_cast<double>(beam.pos.y);
    for (qsizetype index = 0; index < beam.sections.size(); ++index) {
        const QcBeamSection &section = beam.sections.at(index);
        const double top = bottom + section.height + (index == beam.sections.size() - 1 ? QcBeamMesher::above : 0.0);
        const std::array<quint8, 4> tint = tintOf(section.colour, look.strength);
        const QList<Span> spans = segmentsOf(bottom, top, phase);
        for (const Span &span : spans) {
            addRing(mesh, ring, span, tint, look.tile);
        }
        bottom = top;
    }
}

double phaseAt(double seconds)
{
    return std::fmod(seconds * QcBeamMesher::scrollPerSecond, QcBeamMesher::segmentLength);
}

} // namespace

namespace QcBeamMesher {

void appendCore(QcEntityMesh &mesh, const QcMsg::BeaconBeam &beam, const QcVec3 &camera, double seconds, const QRectF &tile)
{
    const Ring ring = ringOf(beam, halfWidth, seconds * turnPerSecond * std::numbers::pi / 180.0);
    if (!isInside(ring, camera)) {
        appendColumn(mesh, beam, ring, phaseAt(seconds), {tile, 1.0});
    }
}

void appendGlow(QcEntityMesh &mesh, const QcMsg::BeaconBeam &beam, const QcVec3 &camera, double seconds, const QRectF &tile)
{
    const Ring ring = ringOf(beam, glowHalfWidth, 0.0);
    if (!isInside(ring, camera)) {
        appendColumn(mesh, beam, ring, phaseAt(seconds), {tile, glowStrength});
    }
}

int heightOf(const QcMsg::BeaconBeam &beam)
{
    int height = above;
    for (const QcBeamSection &section : beam.sections) {
        height += section.height;
    }
    return height;
}

QcAabb boxOf(const QcMsg::BeaconBeam &beam)
{
    const double radius = glowHalfWidth * std::numbers::sqrt2;
    const double x = beam.pos.x + 0.5;
    const double z = beam.pos.z + 0.5;
    return {{x - radius, static_cast<double>(beam.pos.y), z - radius}, {x + radius, static_cast<double>(beam.pos.y + heightOf(beam)), z + radius}};
}

} // namespace QcBeamMesher
