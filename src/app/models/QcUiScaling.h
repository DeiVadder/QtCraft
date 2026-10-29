// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcUiScale.h"

#include <QFont>
#include <QObject>
#include <QSizeF>
#include <QtQmlIntegration/qqmlintegration.h>

// The UI scale for QML as the singleton `UiScaling` (decision E-725): Main.qml gives it the
// player's choice (GameController.settings.uiScale) and the window's size;
// it offers the factor of the screens (`scale`) and of the HUD (`hudScale`), each as large as
// chosen but no larger than lets that part fit into the window. The QML singleton `Ui` turns them
// into sizes (Ui.px, Ui.hudPx, the font sizes); components take their sizes from there.
class QcUiScaling : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(UiScaling)
    QML_SINGLETON
    // UiScaling.Step.Percent75 … Percent200 (Auto, from older settings files, is 100 %).
    Q_PROPERTY(QcUiScaling::Step setting READ setting WRITE setSetting NOTIFY inputsChanged FINAL)
    // The window's content size in logical pixels.
    Q_PROPERTY(QSizeF windowSize READ windowSize WRITE setWindowSize NOTIFY inputsChanged FINAL)
    // The choice times the window's factor (decision E-875), before fitting.
    Q_PROPERTY(double requested READ requested NOTIFY factorsChanged FINAL)
    // Screens and menus: laid out for 1024 x 640 at 1.0.
    Q_PROPERTY(double scale READ scale NOTIFY factorsChanged FINAL)
    // HUD: laid out for 640 x 360 at 1.0.
    Q_PROPERTY(double hudScale READ hudScale NOTIFY factorsChanged FINAL)
    // The application's font at the screens' scale: the window's font for its controls.
    Q_PROPERTY(QFont font READ font NOTIFY factorsChanged FINAL)

public:
    // QcUiScale::Step for QML, in its order.
    enum class Step { Auto, Percent75, Percent100, Percent125, Percent150, Percent175, Percent200 };
    Q_ENUM(Step)

    explicit QcUiScaling(QObject *parent = nullptr);

    static Step fromValue(QcUiScale::Step step) { return static_cast<Step>(step); }
    static QcUiScale::Step toValue(Step step) { return static_cast<QcUiScale::Step>(step); }
    // Whether `step` is one of the enumeration's values (QML may pass any number).
    static bool known(Step step);

    Step setting() const { return fromValue(m_setting); }
    void setSetting(Step step);
    QSizeF windowSize() const { return m_windowSize; }
    void setWindowSize(QSizeF size);
    double requested() const { return m_requested; }
    double scale() const { return m_scale; }
    double hudScale() const { return m_hudScale; }
    QFont font() const;

signals:
    void inputsChanged();
    // Only when a factor differs, so a window resized without crossing a step re-lays out nothing.
    void factorsChanged();

private:
    // Computes the factors again after an input changed.
    void update();

    QcUiScale::Step m_setting = QcUiScale::defaultStep;
    QSizeF m_windowSize = QcUiScale::windowReferenceSize;
    double m_requested = 1.0;
    double m_scale = 1.0;
    double m_hudScale = 1.0;
};
