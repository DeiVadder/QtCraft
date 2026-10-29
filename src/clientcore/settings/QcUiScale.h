// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSizeF>
#include <QString>

#include <optional>

// The size of the HUD and the screens (decisions E-725, E-875): the player's choice (a step from
// 75 % to 200 %) times the window's factor, which grows the UI with large windows, and how far that
// may go in a window before the UI would no longer fit. Pure functions; QcUiScaling applies them
// for QML.
namespace QcUiScale {

// The choices of the settings screen, stored as "uiScale" ("auto", "75", …, "200"). Auto is the
// choice of older settings files and means 100 % (the window's factor applies to every step).
enum class Step { Auto, Percent75, Percent100, Percent125, Percent150, Percent175, Percent200 };

inline constexpr Step defaultStep = Step::Percent100;
// The window the screens are laid out for at 100 % (Main.qml's minimum size): a larger factor
// needs a window that much larger.
inline constexpr QSizeF screenDesignSize{1024.0, 640.0};
// The HUD needs less room (hotbar, status rows, one hint line), so it may grow further.
inline constexpr QSizeF hudDesignSize{640.0, 360.0};
// The window whose UI the steps mean as they are (factor 1.0): about a MacBook's screen in logical
// pixels (1512 x 982), where the screens were designed. Window sizes are logical pixels: the
// system's display scaling (Retina, Windows and Linux scale settings) is already in them, so the
// factor adds nothing for density; it makes up for large monitors at 100 % display scaling
// (2560 x 1440: 1.5).
inline constexpr QSizeF windowReferenceSize{1550.0, 1000.0};
inline constexpr double maxWindowFactor = 3.0;

// "auto", "75", …, "200".
[[nodiscard]] QString name(Step step);
[[nodiscard]] std::optional<Step> fromName(const QString &text);
// 0.75 … 2.0 for the steps chosen relative to the window; Auto has no factor of its own
// (std::nullopt, used as 1.0).
[[nodiscard]] std::optional<double> fixedFactor(Step step);
// The window's factor: its logical size against windowReferenceSize (the smaller ratio), rounded
// to an eighth and kept within 1.0 … maxWindowFactor (a small window never shrinks the UI).
[[nodiscard]] double windowFactor(QSizeF window);
// The factor of `step` in this window: the step's factor times windowFactor.
[[nodiscard]] double requestedFactor(Step step, QSizeF window);
// `requested`, at most as large as lets a UI laid out for `designSize` fit into `window` (rounded
// down to an eighth); 100 % and less always stay, so a window below the design size keeps today's
// layout rather than shrinking.
[[nodiscard]] double fittedFactor(double requested, QSizeF window, QSizeF designSize);

} // namespace QcUiScale
