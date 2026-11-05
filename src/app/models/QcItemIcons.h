// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>

class QcBlockRenderTable;
class QcContentRegistry;
class QcItemIconProvider;
class QcTextureAtlas;

// The item icons QML shows (QcItemIconProvider), drawn from the current content: the running
// world's or, in the menu, the content built for the handbook.
class QcItemIcons
{
public:
    void setProvider(QcItemIconProvider *provider);
    // The provider holds icons of the current content.
    bool isReady() const { return m_ready; }
    void invalidate() { m_ready = false; }
    // Blocks are drawn as their shape when a render table is given, else as their flat texture.
    void draw(const QcContentRegistry &content, const QcTextureAtlas &atlas, const QcBlockRenderTable *renderTable);

private:
    // The engine owns the provider and may delete it first (the game outlives the QML engine).
    QPointer<QcItemIconProvider> m_provider;
    bool m_ready = false;
};
