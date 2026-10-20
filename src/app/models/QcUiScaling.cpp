#include "app/models/QcUiScaling.h"

#include "app/debug/QcAppLogging.h"

#include <QGuiApplication>

#include <cmath>

namespace {

using Step = QcUiScaling::Step;
static_assert(int(Step::Auto) == int(QcUiScale::Step::Auto) && int(Step::Percent75) == int(QcUiScale::Step::Percent75)
                  && int(Step::Percent100) == int(QcUiScale::Step::Percent100)
                  && int(Step::Percent125) == int(QcUiScale::Step::Percent125)
                  && int(Step::Percent150) == int(QcUiScale::Step::Percent150)
                  && int(Step::Percent175) == int(QcUiScale::Step::Percent175)
                  && int(Step::Percent200) == int(QcUiScale::Step::Percent200),
              "QcUiScaling::Step follows QcUiScale::Step");

} // namespace

QcUiScaling::QcUiScaling(QObject *parent)
    : QObject(parent)
{
    update();
}

bool QcUiScaling::known(Step step)
{
    return step >= Step::Auto && step <= Step::Percent200;
}

void QcUiScaling::setSetting(Step step)
{
    if (!known(step)) {
        qCWarning(lcGame) << "UI scale: ignoring unknown step" << int(step);
        return;
    }
    // The same step again changes nothing (Main.qml binds it on every settings change).
    if (toValue(step) == m_setting) {
        return;
    }

    m_setting = toValue(step);
    emit inputsChanged();
    update();
}

void QcUiScaling::setWindowSize(QSizeF size)
{
    if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() < 0.0 || size.height() < 0.0) {
        qCWarning(lcGame) << "UI scale: ignoring window size" << size << "(negative or not finite)";
        return;
    }
    // An empty window (before the first show) changes nothing: the last size stays.
    if (size == m_windowSize || size.isEmpty()) {
        return;
    }

    m_windowSize = size;
    emit inputsChanged();
    update();
}

QFont QcUiScaling::font() const
{
    QFont font = QGuiApplication::font();
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(font.pointSizeF() * m_scale);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qRound(font.pixelSize() * m_scale));
    }
    return font;
}

void QcUiScaling::update()
{
    const double requested = QcUiScale::requestedFactor(m_setting, m_windowSize);
    const double scale = QcUiScale::fittedFactor(requested, m_windowSize, QcUiScale::screenDesignSize);
    const double hudScale = QcUiScale::fittedFactor(requested, m_windowSize, QcUiScale::hudDesignSize);
    if (requested == m_requested && scale == m_scale && hudScale == m_hudScale) {
        return;
    }

    m_requested = requested;
    m_scale = scale;
    m_hudScale = hudScale;
    emit factorsChanged();
}
