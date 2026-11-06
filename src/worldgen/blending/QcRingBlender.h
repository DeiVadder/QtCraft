// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "common/world/QcChunkSet.h"
#include "common/world/QcConversionControl.h"
#include "worldgen/blending/QcBlendField.h"

#include <QByteArray>
#include <QList>

#include <functional>
#include <optional>

class QcWorldGenerator;

/*! \brief A ring of chunks around the old land of a world that blends into a new generator version
    (decisions E-1040, E-1041): the part of the switch with a transition that knows no files.

    plan() finds the ring, blend() generates it with both versions on several threads and returns
    each chunk as its caller encoded it. */
namespace QcRingBlender {

/*! \brief A blended chunk, encoded. \qcfields */
struct Blended
{
    QcChunkPos pos;
    QByteArray blob;
};

/*! \brief The two versions to blend and the threads that do it. \qcfields */
struct Job
{
    /*! The dimension's generator with the ring already in its version map: it builds the
        structures of the ring. */
    const QcWorldGenerator &older;
    /*! The new version, building no structure in the ring. */
    const QcWorldGenerator &newer;
    /*! The threads to use; 0: as many as the machine has, 8 at most. The result is the same. */
    int threads = 0;
};

/*! Returns the field of a ring \a widthChunks wide around \a old: the structures \a older builds
    wholly within the old land and the ring are held, and the ring grows around them so that they
    have an edge of their own. After six rounds it stops growing: all that is held by then keeps
    this ring (QcBlendField::isCut()). Asks \a control between steps (stage Blend); none when it
    stops. */
[[nodiscard]] std::optional<QcBlendField> plan(const QcWorldGenerator &older, const QcChunkSet &old, int widthChunks, const QcConversionControl &control);

/*! Returns every chunk of the ring of \a field blended as \a job says with the patches from
    \a seed, each encoded by \a encode (on one of the threads), reporting to \a control (stage
    Blend); none when it stops. Holds the shapes of the ring and the encoded chunks, not the chunks
    themselves: each is generated twice, for its shape and for its blocks. */
[[nodiscard]] std::optional<QList<Blended>> blend(const Job &job, const QcBlendField &field, quint64 seed,
                                                  const std::function<QByteArray(const QcChunk &)> &encode, const QcConversionControl &control);

} // namespace QcRingBlender
