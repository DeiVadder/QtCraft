#include "common/core/QcCommandSyntax.h"

#include <QRegularExpression>

#include <algorithm>

#include <array>

using namespace Qt::StringLiterals;

namespace QcCommandSyntax {

namespace {

using Type = Parameter::Type;
using Kind = Parameter::Kind;

// Splits `text` at `separator` where no bracket is open.
QStringList splitOutside(QStringView text, QStringView separator)
{
    QStringList parts;
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        depth += (c == u'<' || c == u'[') ? 1 : (c == u'>' || c == u']') ? -1 : 0;
        if (depth == 0 && text.mid(i).startsWith(separator)) {
            parts.append(text.mid(start, i - start).toString());
            start = i + separator.size();
            i = start - 1;
        }
    }
    parts.append(text.mid(start).toString());
    return parts;
}

// The type a placeholder's name stands for.
Type typeOf(const QString &name)
{
    static const QRegularExpression coordinate(u"^[xyz][12]?$"_s);
    static const QRegularExpression range(u"^\\d+-\\d+$"_s);
    static constexpr std::array<QStringView, 12> integers{u"count",  u"seconds", u"level", u"amount", u"food points", u"radius",
                                                          u"ticks", u"n",       u"days",  u"value",  u"points",      u"levels"};
    static constexpr std::array<QStringView, 7> texts{u"reason", u"label", u"values", u"message", u"text", u"command", u"line"};
    const QString lower = name.toLower();
    if (lower.contains(u"player")) {
        return Type::Player;
    }
    if (coordinate.match(lower).hasMatch()) {
        return Type::Coordinate;
    }
    if (range.match(lower).hasMatch() || std::ranges::find(integers, QStringView(lower)) != integers.end()) {
        return Type::Integer;
    }
    if (lower.contains(u'=') || std::ranges::find(texts, QStringView(lower)) != texts.end()) {
        return Type::Text;
    }
    if (lower == u"block"_s || lower == u"item"_s || lower == u"creature"_s) {
        return lower == u"block"_s ? Type::Block : lower == u"item"_s ? Type::Item : Type::Creature;
    }
    return Type::Word;
}

// A parameter from the inside of <...> or [...]: a name, or alternatives separated by '|'.
Parameter placeholder(const QString &inside, Kind kind)
{
    Parameter parameter;
    parameter.kind = kind;
    parameter.name = inside;
    const QStringList alternatives = splitOutside(inside, u"|");
    if (alternatives.size() == 1) {
        parameter.type = typeOf(inside);
        static const QRegularExpression range(u"^(\\d+)-(\\d+)$"_s);
        if (const QRegularExpressionMatch match = range.match(inside); match.hasMatch()) {
            parameter.range = std::pair{match.captured(1).toLongLong(), match.captured(2).toLongLong()};
        }
        return parameter;
    }

    parameter.type = Type::Choice;
    for (const QString &alternative : alternatives) {
        const QString word = alternative.trimmed().section(u' ', 0, 0);
        if (word.startsWith(u'<') && word.endsWith(u'>') && parameter.type == Type::Choice) {
            parameter.type = typeOf(word.mid(1, word.size() - 2));
        } else if (!word.contains(u'<')) {
            parameter.choices.append(word);
        }
    }
    return parameter;
}

// The end of the group that opens at `start` (the index of its closing bracket, or the text's size).
qsizetype groupEnd(QStringView text, qsizetype start)
{
    const QChar open = text[start];
    const QChar close = open == u'<' ? u'>' : u']';
    int depth = 0;
    for (qsizetype end = start; end < text.size(); ++end) {
        depth += text[end] == open ? 1 : text[end] == close ? -1 : 0;
        if (depth == 0) {
            return end;
        }
    }
    return text.size();
}

// The end of the plain word at `start`: a space or a group; a group right after '|' belongs to it
// ("village|<structure>").
qsizetype wordEnd(QStringView text, qsizetype start)
{
    qsizetype end = start + 1;
    while (end < text.size() && !text[end].isSpace() && !((text[end] == u'<' || text[end] == u'[') && text[end - 1] != u'|')) {
        ++end;
    }
    return end;
}

QList<Parameter> parseForm(QStringView text, bool optional);

// The parameters of the group <...> or [...] at `start`.
QList<Parameter> parseGroup(QStringView text, qsizetype start, qsizetype end, bool optional)
{
    const bool square = text[start] == u'[';
    const QString inside = text.mid(start + 1, end - start - 1).toString();
    // An optional group of several placeholders ([<x> <z> [<x2> <z2>]]) gives each of them.
    if (square && inside.startsWith(u'<') && splitOutside(inside, u" ").size() > 1) {
        return parseForm(inside, true);
    }
    return {placeholder(inside, optional || square ? Kind::Optional : Kind::Required)};
}

// A plain word: a literal, or alternatives separated by '|'.
Parameter parseWord(const QString &word, bool optional)
{
    if (word.contains(u'|')) {
        return placeholder(word, optional ? Kind::Optional : Kind::Required);
    }
    return {.name = word, .kind = optional ? Kind::Optional : Kind::Literal, .type = Type::Word, .choices = {}, .range = {}};
}

// The parameters of one form, in order; groups inside an optional group become optional too.
QList<Parameter> parseForm(QStringView text, bool optional)
{
    QList<Parameter> parameters;
    qsizetype i = 0;
    while (i < text.size()) {
        if (text[i].isSpace()) {
            ++i;
        } else if (text[i] == u'<' || text[i] == u'[') {
            const qsizetype end = groupEnd(text, i);
            parameters.append(parseGroup(text, i, end, optional));
            i = end + 1;
        } else {
            const qsizetype end = wordEnd(text, i);
            parameters.append(parseWord(text.mid(i, end - i).toString(), optional));
            i = end;
        }
    }
    return parameters;
}

// Whether `word` fits `parameter` as far as the syntax can tell.
bool fits(const Parameter &parameter, const QString &word)
{
    if (parameter.kind == Kind::Literal) {
        return parameter.name.compare(word, Qt::CaseInsensitive) == 0;
    }
    if (parameter.type == Type::Choice) {
        return parameter.choices.contains(word, Qt::CaseInsensitive);
    }
    return true;
}

} // namespace

QList<Form> forms(const QString &usage)
{
    QString text = usage.trimmed();
    if (text.startsWith(u"Usage:"_s)) {
        text = text.mid(6).trimmed();
    }

    QStringList parts;
    const QStringList byCommas = splitOutside(text, u", /");
    for (const QString &byComma : byCommas) {
        const QStringList byOrs = splitOutside(byComma, u" or /");
        for (const QString &byOr : byOrs) {
            parts.append(splitOutside(byOr, u" | "));
        }
    }

    QList<Form> result;
    for (QString part : std::as_const(parts)) {
        part = part.trimmed();
        // The command's own name ("/tp", or "tp" after a split at ", /").
        if (part.startsWith(u'/') || (!result.isEmpty() && text.startsWith(u'/') && part.section(u' ', 0, 0) == text.section(u' ', 0, 0).mid(1))) {
            part = part.section(u' ', 1);
        }
        result.append(Form{parseForm(part, false)});
    }
    return result;
}

QList<Parameter> next(const QList<Form> &forms, const QStringList &words)
{
    QList<Parameter> result;
    for (const Form &form : forms) {
        const QList<Parameter> &parameters = form.parameters;
        // A Text parameter takes the rest of the line.
        const bool textBefore = std::ranges::any_of(parameters.mid(0, words.size()), [](const Parameter &p) { return p.type == Type::Text; });
        if (textBefore || words.size() >= parameters.size()) {
            continue;
        }
        bool matches = true;
        for (qsizetype i = 0; i < words.size() && matches; ++i) {
            matches = fits(parameters[i], words[i]);
        }
        if (matches && !result.contains(parameters[words.size()])) {
            result.append(parameters[words.size()]);
        }
    }
    return result;
}

QLatin1StringView typeName(Parameter::Type type)
{
    static constexpr std::array<const char *, 9> names{"word", "text", "player", "integer", "coordinate", "block", "item", "creature", "choice"};
    return QLatin1StringView(names[std::size_t(type)]);
}

} // namespace QcCommandSyntax
