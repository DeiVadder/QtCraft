// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>

#include <array>

class QcContentRegistry;
class QcTextureAtlas;
struct QcMobDefinition;

/*! \brief The handbook icon of a creature (decision E-944): its model standing in its rest pose,
    turned so that its face looks out of the icon, drawn from above at an angle like a block icon
    (QcBlockIcon) and scaled to fill it.

    Box creatures show their boxes, skeleton creatures the head of their model and outfit; tinted
    layers take the default colours of QcAppearance (a creature's own look, such as a villager's,
    comes from the server only while the world runs). */
namespace QcMobIcon {

/*! Returns the icon of the creature \a mob of \a content, \a size pixels across, textured from
    \a atlas; \a shadedAtlas is QcBlockIcon::shadedAtlases of the atlas level the icons are drawn
    from; returns a transparent icon for a creature without a model. */
[[nodiscard]] QImage render(const QcContentRegistry &content, const QcMobDefinition &mob, const QcTextureAtlas &atlas,
                            const std::array<QImage, 6> &shadedAtlas, int size);

} // namespace QcMobIcon
