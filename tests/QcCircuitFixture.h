#pragma once

#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "worldgen/structures/QcStateRotation.h"

#include <QTest>

// A server with an empty stone floor in the air above the stage, for building Signal Dust circuits
// block by block (decision E-335). Blocks are set as a player's change would be: the circuits are
// told what was there before. Positions are given relative to the area's origin; `turns` and
// `shift` place the same circuit turned and moved, so tests can compare one circuit in every
// rotation and at other coordinates.
class QcCircuitFixture : public QcServerTestFixture
{
    Q_OBJECT

protected:
    // The floor reaches from the origin this far east and south.
    static constexpr int areaSize = 14;
    static constexpr int areaHeight = 8;

    QcBlockPos m_origin;
    int m_turns = 0;

    // Joins and clears the build area: air above a stone floor at y - 1, no creatures spawning.
    void buildArea(QcServerConfig config = {})
    {
        join(std::move(config));
        chat(QStringLiteral("/gamerule mobSpawning false"));
        clearArea(m_feet.offset(-areaSize / 2, 12, -areaSize / 2));
    }

    // Loads the chunks of an area with its origin at `origin` and clears it like buildArea().
    void clearArea(QcBlockPos origin)
    {
        m_origin = origin;
        // With a margin: circuits wait for chunks within two blocks that are not loaded.
        for (int x = -4; x <= areaSize + 4; x += 4) {
            for (int z = -4; z <= areaSize + 4; z += 4) {
                // A changed chunk stays loaded without a player near it.
                m_server->world().loadChunk(m_origin.offset(x, 0, z).chunk());
                m_server->world().setBlock(m_origin.offset(x, -2, z), id("qtcraft:stone"));
            }
        }
        for (int x = -1; x <= areaSize; ++x) {
            for (int z = -1; z <= areaSize; ++z) {
                m_server->world().setBlock(m_origin.offset(x, -1, z), id("qtcraft:stone"));
                for (int y = 0; y < areaHeight; ++y) {
                    m_server->world().setBlock(m_origin.offset(x, y, z), qcAirBlockId);
                }
            }
        }
        // The cleared cells were never circuits (or held an earlier test's): start from a quiet state.
        m_server->circuitState().clear();
    }

    // The world position of local (x, y, z), turned clockwise by m_turns about the area's centre.
    QcBlockPos at(int x, int y, int z) const
    {
        constexpr int last = areaSize - 1;
        int tx = x;
        int tz = z;
        for (int i = 0; i < ((m_turns % 4) + 4) % 4; ++i) {
            const int nx = last - tz;
            tz = tx;
            tx = nx;
        }
        return m_origin.offset(tx, y, tz);
    }

    QcBlockId state(const QString &text) const
    {
        QStringList problems;
        const std::optional<QcBlockId> parsed = m_content->stateId(text.contains(u':') ? text : QStringLiteral("qtcraft:") + text, &problems);
        if (!parsed || !problems.isEmpty()) {
            QcTestUtil::failCase(QStringLiteral("bad state %1: %2").arg(text, problems.join(u", ")));
            return parsed ? *parsed : qcAirBlockId;
        }
        return QcStateRotation::turned(*m_content, *parsed, m_turns);
    }

    // Sets a block (given as authored for no turns) like a player's change.
    void put(int x, int y, int z, const QString &text) { putAt(at(x, y, z), state(text)); }

    void putAt(QcBlockPos pos, QcBlockId block)
    {
        const QcBlockId before = m_server->world().blockAt(pos).value_or(qcAirBlockId);
        QVERIFY(m_server->world().setBlock(pos, block));
        m_server->circuitState().noteChange(m_server->world(), pos, before, block);
    }

    // The state at local (x, y, z), turned back, as "name[a=x,...]" without the namespace.
    QString look(int x, int y, int z) const
    {
        const std::optional<QcBlockId> block = m_server->world().blockAt(at(x, y, z));
        if (!block) {
            return QStringLiteral("unloaded");
        }
        return m_content->stateName(QcStateRotation::turned(*m_content, *block, -m_turns)).remove(QStringLiteral("qtcraft:"));
    }

    QString property(int x, int y, int z, const QString &name) const
    {
        return m_content->property(*m_server->world().blockAt(at(x, y, z)), name);
    }

    int power(int x, int y, int z) const { return property(x, y, z, QStringLiteral("power")).toInt(); }
    bool flag(int x, int y, int z, const QString &name) const { return property(x, y, z, name) == QStringLiteral("true"); }
    bool isBlock(int x, int y, int z, const char *name) const
    {
        return m_content->block(*m_server->world().blockAt(at(x, y, z))).name == QStringLiteral("qtcraft:") + QLatin1StringView(name);
    }

    // A line of dust from (x0, z) eastwards (local), `length` long.
    void dustLine(int x0, int y, int z, int length)
    {
        for (int x = x0; x < x0 + length; ++x) {
            put(x, y, z, QStringLiteral("signal_dust"));
        }
    }

    // Switches the lever at local (x, y, z) as a player would.
    void flip(int x, int y, int z) { QVERIFY(m_server->systems().circuits.use(at(x, y, z))); }
};
