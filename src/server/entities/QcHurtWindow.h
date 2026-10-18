#pragma once

#include <QtGlobal>

// The short invulnerability of a player or creature after a hurt (decision E-429), counted in game
// ticks (the server's simulation ticks, independent of /tick rate). A hit with damage outside the
// window deals all of it and opens the window for the game rule's number of ticks
// (playerInvulnerabilityTicks, mobInvulnerabilityTicks; 0: the window never opens and every hit
// counts). During the window, as in the reference, a hit only takes when it is stronger than the
// strongest hit of the window so far and then deals only the difference; it does not lengthen the
// window. A hit without damage opens no window (decision E-427). The state is not saved.
class QcHurtWindow
{
public:
    // How a hit would take: not at all, as a fresh hit (knockback, hurt sound and flash) or as a
    // stronger hit during the window (only the difference, silent and without knockback).
    enum class Take : quint8 { Ignored, Fresh, Stronger };

    // The game rules' default length of the window (the reference's 0.5 s) and their highest value.
    static constexpr int defaultTicks = 10;
    static constexpr int maxTicks = 200;
    // Ticks after a fresh hit with damage during which a creature does not steer (knocked back).
    static constexpr int knockbackTicks = 5;

    // How a hit of `amount` would take now (a hit without damage takes fresh outside the window).
    Take wouldTake(double amount) const;
    // Takes a hit of `amount`: returns the damage it deals (0 when ignored) and opens the window for
    // `ticks` on a fresh hit with damage.
    double take(double amount, int ticks);
    // Takes a hit whatever the window (the void): deals all of `amount` and opens the window anew.
    double force(double amount, int ticks);
    // One game tick passes.
    void tick();
    // Ends the window (commands such as /kill).
    void clear();
    // Shortens an open window to at most `ticks` (a game rule was lowered); 0 ends it.
    void limit(int ticks);

    // Whether the window is open.
    bool active() const { return m_ticksLeft > 0; }
    int ticksLeft() const { return m_ticksLeft; }
    // The strongest hit of the open window (0 when closed).
    double lastAmount() const { return m_lastAmount; }
    // Whether the last fresh hit with damage is so recent that the body is still knocked back.
    bool knockedBack() const { return m_sinceHit < knockbackTicks; }

private:
    void open(double amount, int ticks);

    int m_ticksLeft = 0;
    double m_lastAmount = 0.0;
    int m_sinceHit = knockbackTicks;
};
