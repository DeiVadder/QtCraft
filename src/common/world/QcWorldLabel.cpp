#include "QcWorldLabel.h"

#include <QTextBoundaryFinder>

#include <algorithm>

QString QcWorldLabel::limitedText(const QString &text)
{
    if (text.size() <= maxTextLength) {
        return text;
    }

    // Room for the ellipsis; the cut falls on a grapheme boundary, never inside a surrogate pair or
    // between a letter and its combining marks.
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.setPosition(maxTextLength - 1);
    if (!finder.isAtBoundary()) {
        finder.toPreviousBoundary();
    }
    return text.first(std::max<qsizetype>(0, finder.position())) + QChar(0x2026);
}

QcWorldLabel QcWorldLabel::limited() const
{
    return {position, limitedText(text), limitedText(detail), heading};
}
