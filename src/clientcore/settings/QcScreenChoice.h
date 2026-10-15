#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

// Picks the screen a scripted run opens its window on (--screen / QTCRAFT_SCREEN) and places the
// window fully inside it. Pure logic over screen descriptions, so it is testable without displays.
class QcScreenChoice
{
public:
    struct Screen
    {
        QString name;
        QRect geometry;
        // The panel built into the computer (laptop display), as reported by the platform.
        bool builtIn = false;
        bool primary = false;
    };

    struct Result
    {
        qsizetype index = -1;
        // Why this screen was taken, for the log.
        QString reason;
    };

    // `request`: "builtin" (the built-in panel; else a screen whose name says "built-in"; else the
    // primary screen), a screen index ("0", "1", ...) or a name (exact, else contained, ignoring
    // case; else the primary screen). Empty request or no screens: std::nullopt.
    [[nodiscard]] static std::optional<Result> choose(const QList<Screen> &screens, const QString &request);
    // A window of `size` centred on `available`, shrunk to fit inside it.
    static QRect placeInside(QRect available, QSize size);
};
