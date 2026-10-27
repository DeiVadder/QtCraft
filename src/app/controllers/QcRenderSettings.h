#pragma once

#include "clientcore/settings/QcRenderOptions.h"

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

// The render mode of the world (GameController.settings.rendering, decision E-899): classic or
// realistic, the quality of the realistic mode and its exposure. The game controller hands the
// options to the world view whenever they change.
class QcRenderSettings : public QObject
{
    Q_OBJECT
    // RenderSettings in QML, for its enumerations (RenderSettings.Mode.Realistic).
    QML_NAMED_ELEMENT(RenderSettings)
    QML_UNCREATABLE("Provided by GameController.settings.rendering")
    // RenderSettings.Mode.Classic or Realistic.
    Q_PROPERTY(QcRenderSettings::Mode mode READ mode WRITE setMode NOTIFY changed FINAL)
    // RenderSettings.Quality.Low, Medium or High (realistic mode only).
    Q_PROPERTY(QcRenderSettings::Quality quality READ quality WRITE setQuality NOTIFY changed FINAL)
    // Brightness before tonemapping, minExposure..maxExposure (realistic mode only).
    Q_PROPERTY(double exposure READ exposure WRITE setExposure NOTIFY changed FINAL)
    Q_PROPERTY(double minExposure READ minExposure CONSTANT FINAL)
    Q_PROPERTY(double maxExposure READ maxExposure CONSTANT FINAL)

public:
    // QcRenderOptions::Mode and Quality for QML, in their order.
    enum class Mode { Classic, Realistic };
    Q_ENUM(Mode)
    enum class Quality { Low, Medium, High };
    Q_ENUM(Quality)

    explicit QcRenderSettings(QObject *parent = nullptr);

    Mode mode() const { return static_cast<Mode>(options().mode); }
    void setMode(Mode mode);
    Quality quality() const { return static_cast<Quality>(options().quality); }
    void setQuality(Quality quality);
    double exposure() const { return options().exposure; }
    void setExposure(double exposure);
    static double minExposure() { return QcRenderOptions::minExposure; }
    static double maxExposure() { return QcRenderOptions::maxExposure; }

    // Takes the stored options (checked when they were read).
    void load(QcRenderOptions options);
    // The options to store and to draw with.
    const QcRenderOptions &options() const { return m_options; }

signals:
    void changed();

private:
    // Replaces the options and tells the listeners, unless nothing changed.
    void setOptions(QcRenderOptions options);

    QcRenderOptions m_options;
};
