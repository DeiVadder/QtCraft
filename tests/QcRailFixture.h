#pragma once

#include "QcBlockRules.h"
#include "QcCircuitFixture.h"
#include "QcEntityChecks.h"
#include "QcMinecartMotion.h"
#include "QcRails.h"

#include <algorithm>
#include <array>

// Rails and minecarts in the circuit test area (decisions E-382, E-384): laying rails as a player's
// changes do, reading their tracks back in the area's own sides, and carts placed and read in the
// area's coordinates, so a test runs the same layout in all four rotations.
class QcRailFixture : public QcCircuitFixture
{
    Q_OBJECT

protected:
    static constexpr std::array<QcBlockFace, 4> sides = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};

    // The side `turns` quarter turns clockwise (seen from above) from `side`.
    static QcBlockFace turnedSide(QcBlockFace side, int turns)
    {
        const auto index = std::ranges::find(sides, side) - sides.begin();
        return sides[std::size_t((index + ((turns % 4) + 4)) % 4)];
    }

    QcBlockId railState() const { return id("qtcraft:rail"); }

    // Lays a rail of block `name` at every cell of `cells` (local) and lets each take its shape from the others.
    void lay(const QList<QcBlockPos> &cells, const QString &name = QStringLiteral("rail"))
    {
        for (const QcBlockPos &cell : cells) {
            put(cell.x, cell.y, cell.z, name);
        }
        for (const QcBlockPos &cell : cells) {
            const QcBlockPos pos = at(cell.x, cell.y, cell.z);
            putAt(pos, QcBlockRules::connectedState(m_server->world(), pos, *m_server->world().blockAt(pos)));
        }
    }

    // The ends of the track at local (x, y, z) in local sides, sorted ("east,west"; "+" marks a
    // raised end), or "none".
    QString ends(int x, int y, int z) const
    {
        const std::optional<QcRails::Track> track = QcRails::trackOf(*m_content, *m_server->world().blockAt(at(x, y, z)));
        if (!track) {
            return QStringLiteral("none");
        }
        QStringList names;
        for (const QcRails::End &end : track->ends) {
            names.append(QcBlockRules::faceName(turnedSide(end.side, -m_turns)) + (end.raised ? QStringLiteral("+") : QString()));
        }
        names.sort();
        return names.join(u',');
    }

    // A point given in the area's own coordinates (metres from its origin), in the world, turned like at().
    QcVec3 point(double x, double y, double z) const
    {
        double tx = x;
        double tz = z;
        for (int i = 0; i < ((m_turns % 4) + 4) % 4; ++i) {
            const double nx = areaSize - tz;
            tz = tx;
            tx = nx;
        }
        return {m_origin.x + tx, m_origin.y + y, m_origin.z + tz};
    }

    // A direction in the area's coordinates, in the world.
    QcVec3 direction(double x, double z) const
    {
        double tx = x;
        double tz = z;
        for (int i = 0; i < ((m_turns % 4) + 4) % 4; ++i) {
            const double nx = -tz;
            tz = tx;
            tx = nx;
        }
        return {tx, 0.0, tz};
    }

    // The world position back in the area's coordinates.
    QcVec3 local(const QcVec3 &world) const
    {
        double x = world.x - m_origin.x;
        double z = world.z - m_origin.z;
        for (int i = 0; i < ((m_turns % 4) + 4) % 4; ++i) {
            const double nz = areaSize - x;
            x = z;
            z = nz;
        }
        return {x, world.y - m_origin.y, z};
    }

    // A plain cart standing on the rail at local (x, y, z), moving with the local velocity (dx, dz).
    quint32 cartAt(int x, int y, int z, double dx = 0.0, double dz = 0.0) { return spawnCart(QStringLiteral("qtcraft:minecart"), at(x, y, z), direction(dx, dz)); }

    // A standing cart of `item` (a chest or hopper cart) on the rail at local (x, y, z).
    quint32 cartOf(const QString &item, int x, int y, int z) { return spawnCart(item, at(x, y, z), {}); }

    quint32 spawnCart(const QString &item, QcBlockPos cell, const QcVec3 &velocity)
    {
        const std::optional<QcVec3> spot = m_server->minecarts().restingPlace(cell);
        if (!spot) {
            qFatal("no rail for a cart");
        }
        QcMinecart cart = m_server->carts().fromItem(item);
        cart.motion.position = *spot;
        cart.motion.velocity = velocity;
        const QcMinecart *spawned = m_server->minecarts().spawn(cart);
        if (!spawned) {
            qFatal("cart refused");
        }
        return spawned->id;
    }

    const QcMinecart &cart(quint32 id) { return *m_server->carts().find(id); }

    QStringList entityProblems() const { return QcEntityChecks::problems(m_server->entities()); }
};
