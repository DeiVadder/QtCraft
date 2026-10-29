#pragma once

#include "common/core/QcResult.h"

#include <QDataStream>

class QcAdvancementCatalog;
class QcContentRegistry;

// The advancements in the content sync (QcContentSerializer, decision E-923): everything the client
// shows (tree, icon, texts, frame, handbook link) and the names and requirement groups of the
// criteria; the triggers and conditions stay on the server (the client gets Impossible). Reading
// checks the bounds of the parser.
namespace QcAdvancementSerializer {

void write(QDataStream &stream, const QcAdvancementCatalog &advancements);
[[nodiscard]] QcStatus read(QDataStream &stream, QcContentRegistry &registry);

} // namespace QcAdvancementSerializer
