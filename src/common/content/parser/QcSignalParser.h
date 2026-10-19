#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

struct QcJsonFieldReader;

// Parses the "signal" object of blocks (decision E-333): the kind of component and its numbers, and
// checks that the block has the state properties the kind needs (a lever needs `powered`, the wire
// `power` and its four sides, a repeater `facing`, `delay`, `powered` and `locked` ...).
namespace QcSignalParser {

// The signal definition; `block` is the block read so far (its state properties and container).
[[nodiscard]] QcSignalDefinition parseSignal(const QcBlockDefinition &block, QcJsonFieldReader &signalReader,
                                             const QString &packNamespace);

// Name of a kind as written in data ("dust", "lever", ...), empty for None.
QString kindName(QcSignalKind kind);

} // namespace QcSignalParser
