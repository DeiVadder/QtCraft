// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPlayerInput.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

class QcBenchmarkRun;
class QcGameInput;
class QcHandbookController;
class QcScreenState;
class QcScreenshotHook;
class QcScriptedPad;
struct QcSessionParts;

// Test hooks from the command line for reproducible scenes: view direction, spawn column and
// frozen pose of the next world, chat lines and screens once it plays, and the screenshot, held
// until the world is loaded and the script has run. Owns the benchmark run.
class QcStartupHooks : public QObject
{
    Q_OBJECT

public:
    // What the script opens: screens, the handbook and the player list; all of them outlive this object.
    struct Screens
    {
        QcScreenState &state;
        QcHandbookController &handbook;
        QcGameInput &input;
    };

    QcStartupHooks(const Screens &screens, const QcSessionParts &parts, QcBenchmarkRun *benchmark, QObject *parent = nullptr);

    void setInitialLook(float yaw, float pitch);
    // The player's view direction when a world starts.
    QcPlayerInput initialIntent() const;
    void setSpawnColumn(int x, int z) { m_spawnColumn = std::array<int, 2>{x, z}; }
    [[nodiscard]] std::optional<std::array<int, 2>> spawnColumn() const { return m_spawnColumn; }
    // Freezes the local player's pose ("walk", "sprint", "swing", "swim", "jump", "fall", "sneak", "death").
    void setForcedPose(const QString &pose) { m_forcedPose = pose; }
    QString forcedPose() const { return m_forcedPose; }
    // Chat lines sent and overlays opened once the world is playable. Like chat lines, an overlay
    // "@<ms> <name>" opens that many milliseconds later ("@<ms> " closes the overlay and the
    // handbook, "@<ms> pick:<row>" picks up the stack of an inventory model row in the open screen,
    // "@<ms> hover:<x>,<y>" moves the pointer there, e.g. to show a tooltip, "@<ms> open:<objectName>"
    // opens the list of that combo box or that popup, dialog or menu page, "@<ms> click:<objectName>"
    // clicks that button, "message:<text>" shows the message screen with that text, as after a
    // world that could not start, in the main menu as in a world).
    void setStartupScript(QStringList chatLines, const QStringList &overlays);
    // --handbook: opened once the world plays (at once without --play).
    void setStartupHandbook(const QString &target) { m_startupHandbook = target; }
    // The virtual pad of "@<ms> pad:<step>" lines (QcScriptedPad); they run in the main menu too.
    // The screenshot waits until every queued step has been reported.
    void setScriptedPad(QcScriptedPad *pad);
    // The screenshot to take once the world is loaded.
    void setScreenshotHook(QcScreenshotHook *hook);
    // The screenshot of the main menu (without --play): it waits for the menu and for the steps the
    // menu runs (menuShown()).
    void setMenuScreenshotHook(QcScreenshotHook *hook);
    QcBenchmarkRun *benchmark() const { return m_benchmark; }

    // Without --play: runs the "@<ms> open:", "click:", "hover:" and "pad:" steps in the main menu.
    void menuShown();
    // Runs the script: the world has just become playable.
    void worldPlaying();
    // --overlay furnace|chest: fills the machine that just opened from the hotbar.
    void machineOpened();
    // A villager in view opened its trading screen (--overlay trade): the screenshot follows.
    void tradeOpened();
    // Releases the screenshot once every chunk in view arrived and is meshed.
    void releaseScreenshotWhenLoaded(int viewDistance, bool playing);

private:
    static constexpr int loadedScreenAttempts = 40;
    static constexpr int loadedScreenRetryMs = 50;
    // `view:hide` waits up to viewAttempts * viewRetryMs for the view to hold meshed sections,
    // `view:show` as long for a pending hide (slow builds such as TSan mesh later).
    static constexpr int viewAttempts = 300;
    static constexpr int viewRetryMs = 100;

    void sendStartupChat();
    void openStartupOverlay();
    // Uses the block in view once the startup commands have placed it.
    void openMachineInView();
    void scheduleOverlaySteps();
    // One "@<ms> ..." step: open or close a screen, open the handbook, pick up a stack.
    void runOverlayStep(const QString &step);
    // "key:<action id>" ("key:hideHud", "key:screenshot"): the world action of that key.
    // `hold:<action>` / `release:<action>`: holds down or lets go of the key of a key action.
    void holdKey(const QString &actionId, bool held) const;
    void pressKey(const QString &actionId) const;
    // Presses and lets go of the key `name` (QKeySequence text, "J", "Escape") as the keyboard would:
    // to the world while it takes the input, else to the shown screen; logs the overlay after it.
    void pressWindowKey(const QString &name) const;
    // `view:hide` / `view:show`: hides or shows the world view as the phase binding does; a hide
    // waits for meshed sections, a show for the hide before it (`attemptsLeft` retries).
    void showView(bool shown, int attemptsLeft);
    // Opens the screen of an --overlay name (QcScreenState::overlayFromName).
    void openOverlay(const QString &name) const;
    // "pick:", "open:", "text:" and "click:" steps: they act on a control of a shown screen and wait up to
    // loadedScreenAttempts * loadedScreenRetryMs for its screen to be loaded.
    void runOnLoadedScreen(const QString &step, int attempt);
    // Each returns whether it found what it acts on.
    [[nodiscard]] bool pickInOpenScreen(int row) const;
    void hoverAt(QStringView position) const;
    // "text:<objectName>=<text>": types into a field (the search of the creative inventory).
    [[nodiscard]] bool setText(const QString &assignment) const;
    [[nodiscard]] bool openPopup(const QString &objectName) const;
    // Clicks a shown button ("@<ms> click:<objectName>"), as the mouse would.
    [[nodiscard]] bool clickControl(const QString &objectName) const;
    // Gives a shown control the focus as Tab would ("@<ms> focus:<objectName>").
    [[nodiscard]] bool focusControl(const QString &objectName) const;
    // The control named `objectName` on a shown screen, as pick: wants it: an item that is visible,
    // or with `parentShown` one whose visual parent is (a closed popup or sub-screen); a screen
    // kept loaded but hidden (LazyScreen) does not count.
    QObject *shownControl(const QString &objectName, bool parentShown) const;
    // Holds the screenshot until the matching releaseScreenshot().
    void holdScreenshot() const;
    void releaseScreenshot() const;
    // The screenshot hook of the run (setScreenshotHook()); null without one.
    [[nodiscard]] QcScreenshotHook *screenshotHook() const;

    Screens m_screens;
    const QcSessionParts &m_parts;
    QcBenchmarkRun *m_benchmark = nullptr;
    float m_initialYaw = 0.0f;
    float m_initialPitch = 0.0f;
    std::optional<std::array<int, 2>> m_spawnColumn;
    QString m_forcedPose;
    QStringList m_startupChat;
    QString m_startupOverlay;
    QStringList m_startupOverlaySteps;
    QString m_startupHandbook;
    // --overlay furnace: the screenshot waits until the machine has opened and filled.
    bool m_fillMachineOnOpen = false;
    QPointer<QcScreenshotHook> m_screenshotHook;
    QcScriptedPad *m_scriptedPad = nullptr;
    bool m_screenshotReleased = false;
    // Since the first frame that waited for the world to load, and when it last logged that.
    QElapsedTimer m_screenshotWait; // NOLINT(qc-member-access): a log timer of the screenshot gate, no state worth a setter
    qint64 m_screenshotWaitLogged = 0; // NOLINT(qc-member-access): when the gate's timer last logged, only there
    // The menu's screenshot is held until the menu has scheduled its steps.
    bool m_menuHold = false;
};
