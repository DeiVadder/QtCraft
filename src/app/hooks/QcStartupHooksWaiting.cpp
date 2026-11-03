// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The steps of QcStartupHooks that wait for a screen instead of the wall clock: controls of a
// screen still being made (runOnLoadedScreen()) and presses or key actions tied to a screen
// (press:<key>@<screen>, key:<action>@<screen>).

#include "app/hooks/QcStartupHooks.h"

#include "app/controllers/QcScreenState.h"
#include "app/debug/QcAppLogging.h"

#include <QTimer>

#include <memory>

using namespace Qt::StringLiterals;

void QcStartupHooks::runUntilOverlay(const QString &step)
{
    const qsizetype at = step.lastIndexOf(u'@');
    const QString action = step.left(at);
    const std::optional<QcScreenState::Overlay> wanted = QcScreenState::overlayFromName(step.mid(at + 1));
    if (!wanted) {
        qCWarning(lcGame) << "--overlay" << step << ": no such screen after @";
        return;
    }
    if (action.startsWith(u"press:"_s)) {
        pressWhenShown(step, 0);
        return;
    }

    // The key action ends once the screen showed at all, also when a later step closed it again
    // before the next attempt looked.
    auto *watch = new QObject(this);
    auto shown = std::make_shared<bool>(m_screens.state.overlay() == *wanted);
    connect(&m_screens.state, &QcScreenState::overlayChanged, watch, [this, shown, overlay = *wanted] {
        if (m_screens.state.overlay() == overlay) {
            *shown = true;
        }
    });
    repeatKeyUntilShown(action.mid(4), shown, watch, 0);
}

void QcStartupHooks::pressWhenShown(const QString &step, int attempt)
{
    // runUntilOverlay() checked the screen's name.
    const qsizetype at = step.lastIndexOf(u'@');
    const QString key = step.left(at).mid(6);
    if (QcScreenState::overlayName(m_screens.state.overlay()) == step.mid(at + 1)) {
        if (attempt > 0) {
            qCInfo(lcGame) << "--overlay press:" << key << ": waited" << attempt * untilOverlayRetryMs << "ms for its screen";
        }
        pressWindowKey(key);
        return;
    }
    if (attempt + 1 >= untilOverlayAttempts) {
        qCWarning(lcGame) << "--overlay press:" << key << ": its screen did not show";
        return;
    }

    holdScreenshot();
    QTimer::singleShot(untilOverlayRetryMs, this, [this, step, attempt] {
        pressWhenShown(step, attempt + 1);
        releaseScreenshot();
    });
}

void QcStartupHooks::repeatKeyUntilShown(const QString &actionId, const std::shared_ptr<bool> &shown, QObject *watch, int attempt)
{
    if (*shown || attempt >= untilOverlayAttempts) {
        if (!*shown) {
            qCWarning(lcGame) << "--overlay key:" << actionId << ": its screen did not show";
        }
        watch->deleteLater();
        return;
    }
    // Again every keyRepeatAttempts attempts, so the server's answer to one has time to arrive.
    constexpr int keyRepeatAttempts = 5;
    if (attempt % keyRepeatAttempts == 0) {
        pressKey(actionId);
    }

    holdScreenshot();
    QTimer::singleShot(untilOverlayRetryMs, watch, [this, actionId, shown, watch, attempt] {
        repeatKeyUntilShown(actionId, shown, watch, attempt + 1);
        releaseScreenshot();
    });
}

void QcStartupHooks::runOnLoadedScreen(const QString &step, int attempt)
{
    bool done = false;
    if (step.startsWith(u"pick:"_s)) {
        done = pickInOpenScreen(QStringView(step).mid(5).toInt());
    } else if (step.startsWith(u"open:"_s)) {
        done = openPopup(step.mid(5));
    } else if (step.startsWith(u"click:"_s)) {
        done = clickControl(step.mid(6));
    } else if (step.startsWith(u"focus:"_s)) {
        done = focusControl(step.mid(6));
    } else {
        done = setText(step.mid(5));
    }
    if (done) {
        if (attempt > 0) {
            qCInfo(lcGame) << "--overlay" << step << ": waited" << attempt * loadedScreenRetryMs << "ms for its screen";
        }
        return;
    }

    // Screens are made when they open (LazyScreen, decision E-691): until the one the step needs
    // is there, the step and the screenshot wait for it.
    if (attempt + 1 >= loadedScreenAttempts) {
        qCWarning(lcGame) << "--overlay" << step << ": no such control on a shown screen";
        return;
    }
    holdScreenshot();
    QTimer::singleShot(loadedScreenRetryMs, this, [this, step, attempt] {
        runOnLoadedScreen(step, attempt + 1);
        releaseScreenshot();
    });
}
