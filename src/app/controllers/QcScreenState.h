#pragma once

#include "common/gameplay/QcGameMode.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

struct QcSessionParts;

// What the window shows (decision E-530): the phase (menu, loading, world, error, disconnect),
// its status text, the in-game screen on top of the world and whether the world takes keyboard
// and mouse. Changing the screen closes the session's screens that are no longer shown (crafting,
// machine, trading); the machine, trading and crafting screens of the session and the player's
// death change it here. The input does not know this class's owner: it follows the signals
// (interrupted, mouseFreed, worldInputChanged), which keep the order "forget held keys, close the
// session's screens, overlayChanged, capture the mouse again". Lives in the GUI thread beside
// QcGameController, which owns it and forwards its properties to QML.
class QcScreenState : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    // What the window shows: the main menu, the loading screen, the world, the error screen (a
    // world that could not start or save; statusText says why) or the disconnect screen (the
    // session ended, statusText says why). QML compares against GameController.Phase.Playing etc.
    enum class Phase { Menu, Loading, Playing, Error, Disconnected };
    Q_ENUM(Phase)
    // In-game screen on top of the world (GameController.Overlay in QML): none, the inventory, the
    // creative inventory, an open machine (furnace) or container (chest), a villager's trading
    // screen, the pause menu, the settings (at the general page, the key table, the controller
    // page, the debug options or the texture packs), the character screen, open to LAN, the chat
    // line, the editor of a sign (decision E-766), the stonecutter (decision E-785) or the settings
    // at the display page (decision E-835).
    enum class Overlay {
        None,
        Inventory,
        Creative,
        Furnace,
        Chest,
        Trade,
        Pause,
        Settings,
        Controls,
        Gamepad,
        Debug,
        TexturePacks,
        Character,
        Lan,
        Chat,
        Sign,
        Stonecutter,
        Display,
    };
    Q_ENUM(Overlay)

    explicit QcScreenState(const QcSessionParts &parts, QObject *parent = nullptr);

    // The name of an overlay on the command line (--overlay): "" for none, else "inventory",
    // "creative", "furnace", "chest", "trade", "pause", "settings", "controls", "gamepad", "debug",
    // "texturepacks", "character", "lan", "chat", "sign", "stonecutter" or "display".
    static QString overlayName(Overlay overlay);
    // The pause menu and the screens it opens (settings, character, LAN): the world waits behind
    // them, and the window draws at the background frame rate (decision E-835).
    static bool isMenu(Overlay overlay);
    [[nodiscard]] static std::optional<Overlay> overlayFromName(QStringView name);

    Phase phase() const { return m_phase; }
    bool playing() const { return m_phase == Phase::Playing; }
    QString statusText() const { return m_statusText; }
    void setPhase(Phase phase, const QString &status = {});
    bool inputEnabled() const { return m_inputEnabled; }
    void setInputEnabled(bool enabled);
    // In-game screen on top of the world.
    Overlay overlay() const { return m_overlay; }
    void setOverlay(Overlay overlay);
    // How long a closed screen stays loaded (LazyScreen in QML, decision E-691): 60 s, or
    // QTCRAFT_SCREEN_KEEP_ALIVE_MS (tests and measurements unload at once with 0).
    int screenKeepAliveMs() const { return m_screenKeepAliveMs; }
    // QTCRAFT_UI_STATS=1: LazyScreen logs how long each screen took to make (decision E-690).
    bool logScreenCreation() const { return m_logScreenCreation; }
    // Text the chat line starts with when it opens ("/" for the command key).
    QString chatStart() const { return m_chatStart; }
    // Opens the chat line with `start` already typed.
    void openChat(const QString &start);

    // The session opened a crafting table: the inventory screen shows its grid.
    void craftingOpened();
    // The session's game mode changed to one with these `rules`: an open creative inventory becomes
    // the usual one without the catalogue, and a spectator's screens of items close.
    void gameModeChanged(QcGameModes::Rules rules);
    // The session opened a machine: the chest screen for a container, else the furnace screen.
    void machineOpened(bool container);
    // The session closed the machine (or the server did): its screen closes.
    void machineClosed();
    void tradeOpened();
    void tradeClosed();
    // The server opened the editor of a sign.
    void signOpened();
    // The server opened or closed the stonecutter.
    void stonecutterOpened();
    void stonecutterClosed();
    // The player died (screens close, held keys and buttons are forgotten, the mouse is freed for
    // the death screen) or respawned (the world takes the mouse back).
    void healthChanged(bool dead);
    // The session ended with `reason` (translated, E-094): the disconnect screen, unless the menu
    // is shown already.
    void sessionDisconnected(const QString &reason);

signals:
    void phaseChanged();
    void statusChanged();
    void inputEnabledChanged();
    void overlayChanged();
    // Held keys and the latches built on them are to be forgotten (sent before anything else of a
    // change that takes the world's input away).
    void interrupted();
    // The mouse is to be freed now (input disabled, death, disconnect).
    void mouseFreed();
    // Whether the world takes the input may have changed: the mouse capture follows (sent last).
    void worldInputChanged();

private:
    // Stops mining and a drawn bow and closes the session's screens that `m_overlay` no longer shows.
    void closeSessionScreens() const;

    const QcSessionParts &m_parts;
    Phase m_phase = Phase::Menu;
    QString m_statusText;
    bool m_inputEnabled = true;
    Overlay m_overlay = Overlay::None;
    int m_screenKeepAliveMs = 60000;
    bool m_logScreenCreation = qEnvironmentVariableIntValue("QTCRAFT_UI_STATS") == 1;
    QString m_chatStart;
};
