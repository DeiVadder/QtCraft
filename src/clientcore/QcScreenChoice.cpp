#include "QcScreenChoice.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

qsizetype primaryIndex(const QList<QcScreenChoice::Screen> &screens)
{
    const auto primary = std::ranges::find_if(screens, &QcScreenChoice::Screen::primary);
    return primary != screens.end() ? primary - screens.begin() : 0;
}

} // namespace

std::optional<QcScreenChoice::Result> QcScreenChoice::choose(const QList<Screen> &screens, const QString &request)
{
    const QString wanted = request.trimmed();
    if (wanted.isEmpty() || screens.isEmpty()) {
        return std::nullopt;
    }
    const auto fallback = [&screens](const QString &why) { return Result{primaryIndex(screens), why + u", primary screen"_s}; };
    if (wanted.compare(u"builtin"_s, Qt::CaseInsensitive) == 0) {
        if (const auto builtIn = std::ranges::find_if(screens, &Screen::builtIn); builtIn != screens.end()) {
            return Result{builtIn - screens.begin(), u"built-in display"_s};
        }
        for (qsizetype i = 0; i < screens.size(); ++i) {
            if (screens[i].name.contains(u"built-in"_s, Qt::CaseInsensitive) || screens[i].name.contains(u"builtin"_s, Qt::CaseInsensitive)) {
                return Result{i, u"name says built-in"_s};
            }
        }
        return fallback(u"no built-in display"_s);
    }
    bool isNumber = false;
    const qsizetype index = wanted.toLongLong(&isNumber);
    if (isNumber) {
        if (index >= 0 && index < screens.size()) {
            return Result{index, u"index %1"_s.arg(index)};
        }
        return fallback(u"no screen %1"_s.arg(index));
    }
    for (qsizetype i = 0; i < screens.size(); ++i) {
        if (screens[i].name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return Result{i, u"name"_s};
        }
    }
    for (qsizetype i = 0; i < screens.size(); ++i) {
        if (screens[i].name.contains(wanted, Qt::CaseInsensitive)) {
            return Result{i, u"part of the name"_s};
        }
    }
    return fallback(u"no screen named \"%1\""_s.arg(wanted));
}

QRect QcScreenChoice::placeInside(QRect available, QSize size)
{
    const QSize fitted = size.boundedTo(available.size()).expandedTo(QSize(1, 1)).boundedTo(available.size());
    QRect placed(QPoint(), fitted);
    placed.moveCenter(available.center());
    // moveCenter rounds towards the top left; keep the rectangle inside.
    placed.moveLeft(std::clamp(placed.left(), available.left(), available.right() - placed.width() + 1));
    placed.moveTop(std::clamp(placed.top(), available.top(), available.bottom() - placed.height() + 1));
    return placed;
}
