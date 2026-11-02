// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

/*! \brief Picks the screen a scripted run opens its window on (--screen / QTCRAFT_SCREEN) and
    places the window fully inside it.

    Pure logic over screen descriptions, so it is testable without displays. */
class QcScreenChoice
{
public:
    /*! \brief A screen as the platform describes it. */
    struct Screen
    {
        /*! The screen's name (QScreen::name). */
        QString name;
        /*! Its area in virtual desktop coordinates. */
        QRect geometry;
        /*! The panel built into the computer (laptop display), as reported by the platform. */
        bool builtIn = false;
        /*! Whether it is the primary screen. */
        bool primary = false;
    };

    /*! \brief The screen chosen. */
    struct Result
    {
        /*! Its index in the list of screens. */
        qsizetype index = -1;
        /*! Why this screen was taken, for the log. */
        QString reason;
    };

    /*! \a request: "builtin" (the built-in panel; else a screen whose name says "built-in"; else
        the primary screen), a screen index ("0", "1", ...) or a name (exact, else contained,
        ignoring case; else the primary screen). Returns the screen chosen among \a screens;
        std::nullopt for an empty request or no screens. */
    [[nodiscard]] static std::optional<Result> choose(const QList<Screen> &screens, const QString &request);
    /*! Returns a window of \a size centred on \a available, shrunk to fit inside it. */
    static QRect placeInside(QRect available, QSize size);
};
