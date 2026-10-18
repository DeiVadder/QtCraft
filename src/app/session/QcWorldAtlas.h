#pragma once

#include <memory>

class QcBlockRenderTable;
class QcItemIcons;
class QcPreviewSource;
class QcRenderBridge;
class QcTexturePackController;
class QcTextureAtlas;
struct QcSessionParts;

// The texture atlas the running world is drawn with (decision E-301) and the block render table
// built from it: hands both to the mesh scheduler, the render bridge, the world view, the item icons
// and the character previews (decision E-530). Lives in the GUI thread beside QcGameController,
// which owns it; it refers only to parts declared before it.
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
    };

    QcWorldAtlas(QcSessionParts &parts, const Targets &targets);
    ~QcWorldAtlas();

    // The content of the session arrived: builds the atlas of the active texture packs for it and
    // draws the world with it.
    void enterWorld();
    // Draws the world, the item icons and the character previews with `atlas`.
    void apply(const std::shared_ptr<const QcTextureAtlas> &atlas);
    // The world's atlas and render table go (decision E-301); a build for it still running is
    // dropped.
    void leaveWorld();

private:
    QcSessionParts &m_parts;
    Targets m_targets;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    std::shared_ptr<const QcBlockRenderTable> m_renderTable;
};
