#include "QcNameText.h"

#include <QTextBoundaryFinder>

namespace QcNameText {

namespace {

bool isAllowed(char32_t codePoint)
{
    const QChar::Category category = QChar::category(codePoint);
    return !QChar::isNonCharacter(codePoint) && category != QChar::Other_Control && category != QChar::Other_Format
           && category != QChar::Other_Surrogate;
}

} // namespace

qsizetype codePoints(QStringView text)
{
    qsizetype count = 0;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const bool pair = text[index].isHighSurrogate() && index + 1 < text.size() && text[index + 1].isLowSurrogate();
        index += pair ? 1 : 0;
        ++count;
    }
    return count;
}

QString fit(const QString &text, qsizetype maxCodePoints)
{
    if (codePoints(text) <= maxCodePoints) {
        return text;
    }

    // The last grapheme boundary that keeps the code points within the limit.
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
    qsizetype end = 0;
    qsizetype count = 0;
    for (qsizetype boundary = graphemes.toNextBoundary(); boundary > 0; boundary = graphemes.toNextBoundary()) {
        count += codePoints(QStringView(text).sliced(end, boundary - end));
        if (count > maxCodePoints) {
            break;
        }
        end = boundary;
    }
    return text.left(end);
}

QString clean(const QString &text, qsizetype maxCodePoints)
{
    QString allowed;
    allowed.reserve(text.size());
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar unit = text[index];
        if (unit.isHighSurrogate() && index + 1 < text.size() && text[index + 1].isLowSurrogate()) {
            const char32_t codePoint = QChar::surrogateToUcs4(unit, text[index + 1]);
            if (isAllowed(codePoint)) {
                allowed.append(unit);
                allowed.append(text[index + 1]);
            }
            ++index;
        } else if (!unit.isSurrogate() && isAllowed(unit.unicode())) {
            allowed.append(unit);
        }
    }
    return fit(allowed.trimmed(), maxCodePoints).trimmed();
}

} // namespace QcNameText
