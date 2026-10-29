// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcTetherMesher.h"

#include <QHash>
#include <QList>

#include <memory>

class QcContentRegistry;
struct QcClientEntity;
struct QcFamilyDefinition;

// Which parent each child of a family is tied to on the client, from frame to frame (decision
// E-839; the Kjut's threads, drawn by QcTetherMesher). The client knows no family links, so each
// child of a kind that is the `child` of a family with a `tetherTexture` is tied to the nearest
// living parent of that family's kind within its `tetherRange`; a child keeps the parent of the
// last frame while that one stays in range, unless another comes clearly closer (switchShare), so
// threads do not flicker between two parents at about the same distance. When the parent dies, it
// leaves the living entities and the threads are gone with it. The kinds with threads are worked
// out once per content (a new registry from the server). GUI thread only, not locked.
class QcTethers
{
public:
    // Another parent takes a child over only when it is nearer than this share of the distance to
    // the parent of the last frame.
    static constexpr double switchShare = 0.6;

    // The threads among `entities` at `alpha` between the last two ticks.
    [[nodiscard]] QList<QcTetherMesher::Thread> threads(const QHash<quint32, QcClientEntity> &entities,
                                                        const std::shared_ptr<const QcContentRegistry> &content, double alpha);

private:
    // The parent kind and family of a child kind that has a thread.
    struct Parent
    {
        int type = -1;
        const QcFamilyDefinition *family = nullptr;
    };

    // The parent `child` (of a kind with a thread to parents of `kind`) is tied to among
    // `parents` at `alpha`, if any is in range.
    [[nodiscard]] const QcClientEntity *parentOf(const QcClientEntity &child, Parent kind, const QList<const QcClientEntity *> &parents,
                                                 double alpha) const;
    // The parent kinds of the child kinds, worked out anew for a new content.
    void learn(const std::shared_ptr<const QcContentRegistry> &content);

    std::shared_ptr<const QcContentRegistry> m_content;
    QHash<int, Parent> m_parentOf;
    // The parent each child was tied to in the last frame (ids).
    QHash<quint32, quint32> m_matches;
};
