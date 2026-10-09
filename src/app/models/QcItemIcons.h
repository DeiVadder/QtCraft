#pragma once

class QcBlockRenderTable;
class QcContentRegistry;
class QcItemIconProvider;
class QcTextureAtlas;

// The item icons QML shows (QcItemIconProvider), drawn from the current content: the running
// world's or, in the menu, the content built for the handbook.
class QcItemIcons
{
public:
    void setProvider(QcItemIconProvider *provider) { m_provider = provider; }
    // The provider holds icons of the current content.
    bool isReady() const { return m_ready; }
    void invalidate() { m_ready = false; }
    // Blocks are drawn as their shape when a render table is given, else as their flat texture.
    void draw(const QcContentRegistry &content, const QcTextureAtlas &atlas, const QcBlockRenderTable *renderTable);

private:
    QcItemIconProvider *m_provider = nullptr;
    bool m_ready = false;
};
