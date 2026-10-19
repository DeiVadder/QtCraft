#pragma once

#include <QMap>
#include <QString>

class QcContentRegistry;

// Rule values of the server and the shared physics that handbook chapters quote as {name} and
// creature pages show as caps (QcHandbookBuilder), so the handbook follows the code and, where a
// value belongs to a block of the base content (the explosive), that content.
namespace QcHandbookRules {

QMap<QString, double> values(const QcContentRegistry &content);

} // namespace QcHandbookRules
