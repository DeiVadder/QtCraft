// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/hooks/QcScriptedPad.h"

#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcPendingScreens.h"
#include "app/input/QcGamepadUi.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <print>
#include <utility>

using namespace Qt::StringLiterals;

QcScriptedPad::QcScriptedPad(QcGamepadUi &ui, QObject *parent)
    : QObject(parent)
    , m_ui(ui)
{
    m_pad = m_source.connectPad(u"Scripted pad"_s, QcGamepadType::Xbox);
}

bool QcScriptedPad::wanted(const QStringList &overlays)
{
    return std::ranges::any_of(overlays, [](const QString &line) { return line.contains(u" pad:"_s); });
}

std::optional<QcScriptedPad::Step> QcScriptedPad::parse(QStringView step)
{
    if (step.startsWith(u"connect="_s)) {
        const std::optional<QcGamepadType> type = QcGamepadNames::typeFromName(step.mid(8).toString());
        if (!type) {
            qCWarning(lcGame) << "--overlay pad:" << step << ": unknown pad type";
            return std::nullopt;
        }
        return Step{.kind = Step::Kind::Connect, .type = *type};
    }
    if (const qsizetype equals = step.indexOf(u'='); equals > 0) {
        bool valid = false;
        const float value = step.mid(equals + 1).toFloat(&valid);
        const std::optional<QcGamepadAxis> axis = QcGamepadNames::axisFromName(step.left(equals).toString());
        if (!valid || !axis) {
            qCWarning(lcGame) << "--overlay pad:" << step << ": expected '<axis>=<value>'";
            return std::nullopt;
        }
        return Step{.kind = Step::Kind::Axis, .axis = *axis, .value = value};
    }

    const bool hold = step.startsWith(u'+');
    const bool release = step.startsWith(u'-');
    const std::optional<QcGamepadButton> button = QcGamepadNames::buttonFromName(step.mid(hold || release ? 1 : 0).toString());
    if (!button) {
        qCWarning(lcGame) << "--overlay pad:" << step << ": unknown button";
        return std::nullopt;
    }
    const Step::Kind kind = hold ? Step::Kind::Hold : release ? Step::Kind::Release : Step::Kind::Click;
    return Step{.kind = kind, .button = *button};
}

bool QcScriptedPad::run(QStringView step)
{
    const std::optional<Step> parsed = parse(step);
    if (!parsed) {
        return false;
    }

    m_queue.append(*parsed);
    runNext();
    return true;
}

void QcScriptedPad::runNext()
{
    if (m_running || m_queue.isEmpty()) {
        return;
    }

    m_running = true;
    apply(m_queue.takeFirst());
}

void QcScriptedPad::apply(const Step &step)
{
    switch (step.kind) {
    case Step::Kind::Axis:
        m_source.setAxis(m_pad, step.axis, step.value);
        break;
    case Step::Kind::Hold:
    case Step::Kind::Release:
        m_source.setPressed(m_pad, step.button, step.kind == Step::Kind::Hold);
        break;
    case Step::Kind::Connect:
        connectAnother(step.type);
        break;
    case Step::Kind::Click:
        m_source.setPressed(m_pad, step.button, true);
        afterFrames(clickFrames, [this, button = step.button] {
            m_source.setPressed(m_pad, button, false);
            afterFrames(m_reportFrames, [this] { report(); });
        });
        return;
    }
    afterFrames(m_reportFrames, [this] { report(); });
}

void QcScriptedPad::connectAnother(QcGamepadType type)
{
    m_pad = m_source.connectPad(u"Scripted %1 pad"_s.arg(QcGamepadNames::type(type)), type);
}

void QcScriptedPad::afterFrames(int frames, std::function<void()> then)
{
    QQuickWindow *window = m_ui.navigator().window();
    if (!window) {
        QTimer::singleShot(frames * 16, this, std::move(then));
        return;
    }

    m_framesLeft = frames;
    m_afterFrames = std::move(then);
    // frameSwapped may come from the render thread.
    m_frameConnection = connect(window, &QQuickWindow::frameSwapped, this, &QcScriptedPad::onFrameSwapped, Qt::QueuedConnection);
    window->update();
}

void QcScriptedPad::onFrameSwapped()
{
    // A frame queued before the wait ended.
    if (!m_afterFrames) {
        return;
    }
    // A screen the step opened is made over the next frames: the step waits for it (at most
    // QcPendingScreens::maxWaitFrames more frames).
    const QQuickWindow *shown = m_ui.navigator().window();
    const bool pending = shown && m_framesLeft > -QcPendingScreens::maxWaitFrames && QcPendingScreens::any(*shown);
    if (--m_framesLeft > 0 || pending) {
        // A still scene renders only on request.
        if (QQuickWindow *window = m_ui.navigator().window()) {
            window->update();
        }
        return;
    }

    disconnect(m_frameConnection);
    std::exchange(m_afterFrames, {})();
}

void QcScriptedPad::report()
{
    std::println(stderr, "padFocus: {}", m_ui.focusDescription().toStdString());
    std::println(stderr, "padFamily: {}", m_ui.pads()->family().toStdString());
    if (m_ui.radialOpen()) {
        std::println(stderr, "padRadial: {}", m_ui.radialSlot());
    }
    // The on-screen keyboard's preview while it is open.
    const QQuickWindow *window = m_ui.navigator().window();
    const auto *preview = window ? window->findChild<QQuickItem *>(u"keyboardPreview"_s) : nullptr;
    if (preview && preview->isVisible()) {
        std::println(stderr, "padKeyboard: {}", preview->property("text").toString().toStdString());
    }
    std::fflush(stderr);

    m_running = false;
    emit stepReported();
    runNext();
}
