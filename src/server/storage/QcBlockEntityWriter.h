// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "server/storage/QcBlockEntityRecord.h"

#include <QDataStream>

/*! \brief Writes the block entities of one chunk in the form QcBlockEntityCodec reads (decisions
    E-358, E-440): the writing half of the codec, which QcBlockEntityCodec::encode uses. */
namespace QcBlockEntityWriter {

/*! Writes the tables of \a record to \a stream, items named after \a content: every table with
    entries, each entry framed with its version and length and followed by the tail a newer
    version kept for it (written back under that version); an empty table is left out unless a
    newer version's tail of it is kept. */
void write(QDataStream &stream, const QcBlockEntityRecord &record, const QcContentRegistry &content);

} // namespace QcBlockEntityWriter
