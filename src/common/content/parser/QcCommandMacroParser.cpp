#include "QcCommandMacroParser.h"

#include "QcJsonFieldReader.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

using Macro = QcCommandMacroDefinition;

bool isCommandName(const QString &name)
{
    constexpr qsizetype maxNameLength = 32;
    return !name.isEmpty() && name.size() <= maxNameLength
           && std::ranges::all_of(name, [](QChar c) { return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'_'; });
}

// Highest $1..$9 of a line; -1 for a '$' that is neither one of them nor $*.
int highestArgument(const QString &line)
{
    int highest = 0;
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line[i] != u'$') {
            continue;
        }

        const QChar next = i + 1 < line.size() ? line[i + 1] : QChar();
        if (next >= u'1' && next <= u'9') {
            highest = std::max(highest, next.digitValue());
        } else if (next != u'*') {
            return -1;
        }
        ++i;
    }
    return highest;
}

void parseLines(const QJsonObject &json, Macro &macro, QcJsonFieldReader &reader)
{
    const QJsonArray lines = json.value(u"run"_s).toArray();
    if (!json.value(u"run"_s).isArray() || lines.isEmpty() || lines.size() > Macro::maxLines) {
        reader.fail(u"'run' must be an array of 1 to %1 command lines"_s.arg(Macro::maxLines));
        return;
    }

    for (const auto &value : lines) {
        const QString line = value.toString().trimmed();
        const int highest = highestArgument(line);
        if (!value.isString() || !line.startsWith(u'/') || line.size() > Macro::maxLineLength || highest < 0) {
            reader.fail(u"each line of 'run' must be a command starting with '/' of at most %1 characters, with $1..$9 or $* only"_s.arg(
                Macro::maxLineLength));
            return;
        }
        macro.requiredArguments = std::max(macro.requiredArguments, highest);
        macro.lines.append(line);
    }
}

} // namespace

namespace QcCommandMacroParser {

QcResult<QcCommandMacroDefinition> parseCommandMacro(const QJsonObject &json, const QString &packNamespace)
{
    Q_UNUSED(packNamespace) // Players type the bare name; a later pack replaces an earlier macro.
    QcJsonFieldReader reader{json, u"command"_s, {}};
    Macro macro;
    macro.name = reader.string(u"name"_s);
    if (!isCommandName(macro.name)) {
        return qcError(reader.ok() ? u"command: 'name' must be 1 to 32 lower-case letters, digits or '_'"_s : reader.error);
    }

    reader.context = u"command '%1'"_s.arg(macro.name);
    const QString permission = reader.string(u"permission"_s, u"operator"_s);
    if (permission != u"operator"_s && permission != u"player"_s) {
        reader.fail(u"'permission' must be 'player' or 'operator'"_s);
    }
    macro.operatorOnly = permission == u"operator"_s;
    macro.usage = reader.string(u"usage"_s, u"/"_s + macro.name).left(Macro::maxTextLength);
    macro.description = reader.string(u"description"_s).left(Macro::maxTextLength);
    if (reader.ok()) {
        parseLines(json, macro, reader);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return macro;
}

} // namespace QcCommandMacroParser
