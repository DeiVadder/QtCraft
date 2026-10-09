#pragma once

#include <QMap>
#include <QString>

// Rule values of the server and the shared physics that handbook chapters quote as {name} and
// creature pages show as caps (QcHandbookBuilder), so the handbook follows the code.
namespace QcHandbookRules {

QMap<QString, double> values();

} // namespace QcHandbookRules
