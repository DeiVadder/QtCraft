#include "app/controllers/QcRenderSettings.h"

#include "app/debug/QcAppLogging.h"

QcRenderSettings::QcRenderSettings(QObject *parent)
    : QObject(parent)
{
}

void QcRenderSettings::setMode(Mode mode)
{
    if (mode != Mode::Classic && mode != Mode::Realistic) {
        qCWarning(lcGame) << "Ignoring unknown render mode" << static_cast<int>(mode);
        return;
    }

    QcRenderOptions options = this->options();
    options.mode = static_cast<QcRenderOptions::Mode>(mode);
    setOptions(options);
}

void QcRenderSettings::setQuality(Quality quality)
{
    if (quality != Quality::Low && quality != Quality::Medium && quality != Quality::High) {
        qCWarning(lcGame) << "Ignoring unknown render quality" << static_cast<int>(quality);
        return;
    }

    QcRenderOptions options = this->options();
    options.quality = static_cast<QcRenderOptions::Quality>(quality);
    setOptions(options);
}

void QcRenderSettings::setExposure(double exposure)
{
    // The slider stays inside the range; anything else came from outside.
    if (!QcRenderOptions::isExposure(exposure)) {
        qCWarning(lcGame) << "Ignoring exposure" << exposure << "outside" << minExposure() << ".." << maxExposure();
        return;
    }

    QcRenderOptions options = this->options();
    options.exposure = exposure;
    setOptions(options);
}

void QcRenderSettings::load(QcRenderOptions options)
{
    setOptions(options);
}

void QcRenderSettings::setOptions(QcRenderOptions options)
{
    // The same value again changes nothing (the screen's boxes and slider set it on every move).
    if (options == this->options()) {
        return;
    }

    m_options = options;
    emit changed();
}
