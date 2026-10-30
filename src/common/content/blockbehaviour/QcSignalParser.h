// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

struct QcJsonFieldReader;

/*! \brief Parses the "signal" object of blocks (decision E-333): the kind of component and its
    numbers, and checks that the block has the state properties the kind needs (a lever needs
    \c {powered}, the wire \c {power} and its four sides, a repeater \c {facing}, \c {delay},
    \c {powered} and \c {locked} ...). */
namespace QcSignalParser {

/*! Returns the signal definition read by \a signalReader, which also takes the failures, with
    names qualified in \a packNamespace; \a block is the block read so far (its state properties
    and container). */
[[nodiscard]] QcSignalDefinition parseSignal(const QcBlockDefinition &block, QcJsonFieldReader &signalReader,
                                             const QString &packNamespace);

/*! Returns the name of \a kind as written in data ("dust", "lever", ...), empty for None. */
QString kindName(QcSignalKind kind);

} // namespace QcSignalParser
