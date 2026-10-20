#pragma once

#include <QSizeF>
#include <QString>

#include <optional>

// The size of the HUD and the screens (decision E-725): the player's choice (a fixed step from 75 %
// to 200 % or Auto), the factor Auto picks from the window and the display, and how far a factor
// may go in a window before the UI would no longer fit. Pure functions; QcUiScaling applies them
// for QML.
namespace QcUiScale {

// The choices of the settings screen, stored as "uiScale" ("auto", "75", …, "200").
enum class Step { Auto, Percent75, Percent100, Percent125, Percent150, Percent175, Percent200 };

inline constexpr Step defaultStep = Step::Percent100;
// The window the screens are laid out for at 100 % (Main.qml's minimum size): a larger factor
// needs a window that much larger.
inline constexpr QSizeF screenDesignSize{1024.0, 640.0};
// The HUD needs less room (hotbar, status rows, one hint line), so it may grow further.
inline constexpr QSizeF hudDesignSize{640.0, 360.0};
// The window Auto treats as 100 %. Window sizes are logical pixels: the system's display scaling
// (Retina, Windows and Linux scale settings) is already in them, so Auto adds nothing for density.
inline constexpr QSizeF autoReferenceSize{1280.0, 720.0};
inline constexpr double maxFactor = 2.0;

// "auto", "75", …, "200".
[[nodiscard]] QString name(Step step);
[[nodiscard]] std::optional<Step> fromName(const QString &text);
// 0.75 … 2.0; Auto has no fixed factor (std::nullopt).
[[nodiscard]] std::optional<double> fixedFactor(Step step);
// Auto: the window's logical size against 1280 x 720, rounded down to a quarter and kept within
// 1.0 … 2.0 (a small window never shrinks the UI below 100 %).
[[nodiscard]] double autoFactor(QSizeF window);
// The factor of `step` in this window (Auto: autoFactor).
[[nodiscard]] double requestedFactor(Step step, QSizeF window);
// `requested`, at most as large as lets a UI laid out for `designSize` fit into `window` (rounded
// down to an eighth); 100 % and less always stay, so a window below the design size keeps today's
// layout rather than shrinking.
[[nodiscard]] double fittedFactor(double requested, QSizeF window, QSizeF designSize);

} // namespace QcUiScale
