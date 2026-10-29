// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <memory>
#include <utility>

class QcBlockRenderTable;
class QcHeldMap;
class QcItemIcons;
class QcMaterialAtlas;
class QcPreviewSource;
class QcRenderBridge;
class QcTexturePackController;
class QcTextureAtlas;
struct QcSessionParts;

// The texture atlas the running world is drawn with (decision E-301) and the block render table
// built from it: hands both to the mesh scheduler, the render bridge, the world view, the item icons,
// the character previews (decision E-530) and the map colours (decision E-801). Lives in the GUI
// thread beside QcGameController, which owns it; it refers only to parts declared before it.
class QcWorldAtlas
{
public:
    // What the atlas is handed to; all of them outlive this object.
    struct Targets
    {
        QcTexturePackController &texturePacks;
        QcItemIcons &icons;
        QcPreviewSource &preview;
        QcRenderBridge &render;
        // The map colours come from the atlas (decision E-801).
        QcHeldMap &heldMap;
    };

    QcWorldAtlas(QcSessionParts &parts, const Targets &targets);
    ~QcWorldAtlas();
    Q_DISABLE_COPY_MOVE(QcWorldAtlas)

    // The content of the session arrived: builds the atlas of the active texture packs for it and
    // draws the world with it.
    void enterWorld();
    // Draws the world, the item icons, the character previews and maps with `atlas`, the realistic
    // mode with its `materials` (null without them). Only the maps change for the atlas drawn
    // already; they are handed to the world view, not kept (decision E-912).
    void apply(const std::shared_ptr<const QcTextureAtlas> &atlas, std::shared_ptr<const QcMaterialAtlas> materials = {});
    // The world's atlas and render table go (decision E-301); a build for it still running is
    // dropped.
    void leaveWorld();

private:
    // The atlas the world is drawn with (null without a world).
    [[nodiscard]] const std::shared_ptr<const QcTextureAtlas> &atlas() const { return m_atlas; }
    void setAtlas(std::shared_ptr<const QcTextureAtlas> atlas) { m_atlas = std::move(atlas); }

    QcSessionParts &m_parts;
    Targets m_targets;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    std::shared_ptr<const QcBlockRenderTable> m_renderTable;
};
