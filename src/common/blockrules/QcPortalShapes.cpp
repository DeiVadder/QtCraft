// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/blockrules/QcPortalShapes.h"

#include "common/content/registry/QcContentRegistry.h"

#include <array>

using namespace Qt::StringLiterals;

namespace {

using Axis = QcPortalFrame::Axis;

// One block along the width and along the height of a frame standing as `axis`.
struct Steps
{
    QcBlockPos across;
    QcBlockPos up;
};

constexpr Steps stepsOf(Axis axis)
{
    switch (axis) {
    case Axis::X:
        return {{1, 0, 0}, {0, 1, 0}};
    case Axis::Z:
        return {{0, 0, 1}, {0, 1, 0}};
    case Axis::Y:
        return {{1, 0, 0}, {0, 0, 1}};
    }
    return {{1, 0, 0}, {0, 1, 0}};
}

QcBlockPos moved(QcBlockPos pos, QcBlockPos step, int count)
{
    return pos.offset(step.x * count, step.y * count, step.z * count);
}

// Looks at the blocks of one portal in one world.
class FrameLook
{
public:
    FrameLook(const QcBlockAccess &world, const QcPortalDefinition &portal, QcPortalShapes::Inside inside)
        : m_world(world)
        , m_content(world.content())
        , m_portal(portal)
        , m_inside(inside)
    {
    }

    // Whether the cell may be inside the frame.
    bool accepts(QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = m_world.blockAt(pos);
        if (!block || !m_content.isValidBlock(*block)) {
            return false;
        }
        if (m_inside == QcPortalShapes::Inside::Lit) {
            return m_content.block(*block).name == m_portal.block;
        }
        return *block == qcAirBlockId || m_content.block(*block).fire.isFire;
    }

    bool isFrame(QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = m_world.blockAt(pos);
        return block && QcPortalShapes::isFrame(m_content, m_portal, *block, !m_portal.insertProperty.isEmpty());
    }

    // The frame standing as `axis` whose inside holds `cell`.
    std::optional<QcPortalFrame> frameAt(QcBlockPos cell, Axis axis) const
    {
        const Steps steps = stepsOf(axis);
        const int maxWidth = QcPortalDefinition::innerOf(m_portal.maxWidth);
        const int maxHeight = QcPortalDefinition::innerOf(m_portal.maxHeight);
        const std::optional<QcBlockPos> bottom = walkBack(cell, steps.up, maxHeight);
        const std::optional<QcBlockPos> origin = bottom ? walkBack(*bottom, steps.across, maxWidth) : std::nullopt;
        if (!origin) {
            return std::nullopt;
        }

        QcPortalFrame frame{*origin, axis, extent(*origin, steps.across, maxWidth), extent(*origin, steps.up, maxHeight)};
        const bool sized = frame.width >= QcPortalDefinition::innerOf(m_portal.minWidth) && frame.height >= QcPortalDefinition::innerOf(m_portal.minHeight);
        if (!sized || !isComplete(frame)) {
            return std::nullopt;
        }
        return frame;
    }

private:
    // From `pos` back along `step` over accepted cells to the last one before a frame block.
    std::optional<QcBlockPos> walkBack(QcBlockPos pos, QcBlockPos step, int limit) const
    {
        for (int count = 0; count < limit; ++count) {
            const QcBlockPos before = moved(pos, step, -1);
            if (isFrame(before)) {
                return pos;
            }
            if (!accepts(before)) {
                return std::nullopt;
            }
            pos = before;
        }
        return std::nullopt;
    }

    // The accepted cells from `origin` along `step` up to a frame block; 0 when too many.
    int extent(QcBlockPos origin, QcBlockPos step, int limit) const
    {
        for (int count = 0; count <= limit; ++count) {
            const QcBlockPos pos = moved(origin, step, count);
            if (isFrame(pos)) {
                return count;
            }
            if (!accepts(pos)) {
                return 0;
            }
        }
        return 0;
    }

    // Whether every inner cell is accepted and the four sides are frame blocks (corners free).
    bool isComplete(const QcPortalFrame &frame) const
    {
        for (int across = 0; across < frame.width; ++across) {
            if (!isFrame(frame.at(across, -1)) || !isFrame(frame.at(across, frame.height))) {
                return false;
            }
            for (int up = 0; up < frame.height; ++up) {
                if (!accepts(frame.at(across, up))) {
                    return false;
                }
            }
        }
        for (int up = 0; up < frame.height; ++up) {
            if (!isFrame(frame.at(-1, up)) || !isFrame(frame.at(frame.width, up))) {
                return false;
            }
        }
        return true;
    }

    const QcBlockAccess &m_world;
    const QcContentRegistry &m_content;
    const QcPortalDefinition &m_portal;
    QcPortalShapes::Inside m_inside;
};

// Whether `block` is a portal block that fills a frame standing as `axis` in the support rule.
bool joins(const QcContentRegistry &content, QcBlockId block, QcBlockId portal)
{
    if (!content.isValidBlock(block)) {
        return false;
    }
    if (content.isSameBlock(block, portal)) {
        return content.property(block, u"axis"_s) == content.property(portal, u"axis"_s);
    }
    const QString &name = content.block(portal).name;
    return std::ranges::any_of(content.worldgen().portals(), [&](const QcPortalDefinition &definition) {
        return definition.block == name && QcPortalShapes::isFrame(content, definition, block, false);
    });
}

} // namespace

QcBlockPos QcPortalFrame::at(int across, int up) const
{
    const Steps steps = stepsOf(axis);
    return moved(moved(origin, steps.across, across), steps.up, up);
}

QList<QcBlockPos> QcPortalFrame::cells() const
{
    QList<QcBlockPos> cells;
    cells.reserve(static_cast<qsizetype>(width) * height);
    for (int up = 0; up < height; ++up) {
        for (int across = 0; across < width; ++across) {
            cells.append(at(across, up));
        }
    }
    return cells;
}

bool QcPortalFrame::contains(QcBlockPos pos) const
{
    const QcBlockPos last = at(width - 1, height - 1);
    return pos.x >= origin.x && pos.y >= origin.y && pos.z >= origin.z && pos.x <= last.x && pos.y <= last.y && pos.z <= last.z;
}

QcVec3 QcPortalFrame::arrival() const
{
    const QcVec3 middle = centre();
    if (axis == Axis::Y) {
        return {middle.x, origin.y + 1.0, middle.z};
    }
    return {middle.x, static_cast<double>(origin.y), middle.z};
}

QcVec3 QcPortalFrame::centre() const
{
    const QcBlockPos last = at(width - 1, height - 1);
    return {(origin.x + last.x + 1) / 2.0, (origin.y + last.y + 1) / 2.0, (origin.z + last.z + 1) / 2.0};
}

namespace QcPortalShapes {

std::optional<QcPortalFrame> find(const QcBlockAccess &world, const QcPortalDefinition &portal, QcBlockPos cell, Inside inside)
{
    const FrameLook look(world, portal, inside);
    if (!look.accepts(cell)) {
        return std::nullopt;
    }
    if (portal.shape == QcPortalDefinition::Shape::Ring) {
        return look.frameAt(cell, Axis::Y);
    }
    if (std::optional<QcPortalFrame> frame = look.frameAt(cell, Axis::X)) {
        return frame;
    }
    return look.frameAt(cell, Axis::Z);
}

std::optional<QcPortalFrame> ringBeside(const QcBlockAccess &world, const QcPortalDefinition &portal, QcBlockPos frameBlock)
{
    static constexpr std::array<QcBlockPos, 4> sides{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (const QcBlockPos &side : sides) {
        if (std::optional<QcPortalFrame> frame = find(world, portal, moved(frameBlock, side, 1), Inside::Empty)) {
            return frame;
        }
    }
    return std::nullopt;
}

std::optional<QcBlockId> fillState(const QcContentRegistry &content, const QcPortalDefinition &portal, QcPortalFrame::Axis axis)
{
    const std::optional<QcBlockId> block = content.blockId(portal.block);
    if (!block || axis == Axis::Y || content.block(*block).property(u"axis"_s) == nullptr) {
        return block;
    }
    return content.withProperty(*block, u"axis"_s, axis == Axis::X ? u"x"_s : u"z"_s);
}

bool isFrame(const QcContentRegistry &content, const QcPortalDefinition &portal, QcBlockId block, bool needsInsert)
{
    if (!content.isValidBlock(block) || content.block(block).name != portal.frame) {
        return false;
    }
    return !needsInsert || content.property(block, portal.insertProperty) == portal.insertValue;
}

const QcPortalDefinition *portalOfBlock(const QcContentRegistry &content, QcBlockId block)
{
    if (!content.isValidBlock(block)) {
        return nullptr;
    }
    const QString &name = content.block(block).name;
    const QList<QcPortalDefinition> &portals = content.worldgen().portals();
    const auto it = std::ranges::find(portals, name, &QcPortalDefinition::block);
    return it != portals.end() ? &*it : nullptr;
}

bool isHeld(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    const QcPortalDefinition *portal = portalOfBlock(content, id);
    if (portal == nullptr) {
        return true;
    }

    const QString axis = content.property(id, u"axis"_s);
    const Axis standing = portal->shape == QcPortalDefinition::Shape::Ring ? Axis::Y : (axis == u"z"_s ? Axis::Z : Axis::X);
    const Steps steps = stepsOf(standing);
    for (const QcBlockPos &neighbour : {moved(pos, steps.across, 1), moved(pos, steps.across, -1), moved(pos, steps.up, 1), moved(pos, steps.up, -1)}) {
        const std::optional<QcBlockId> block = world.blockAt(neighbour);
        if (block && !joins(content, *block, id)) {
            return false;
        }
    }
    return true;
}

} // namespace QcPortalShapes
