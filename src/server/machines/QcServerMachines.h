#pragma once

class QcServerSounds;
class QcServerState;

// The machines (furnaces) in the server tick: they work by QcMachineRules, tell comparators and
// hoppers when their slots changed, and crackle while they burn.
class QcServerMachines
{
public:
    // A burning machine crackles on average this often (seconds).
    static constexpr double machineSoundSeconds = 2.5;

    QcServerMachines(QcServerState &state, QcServerSounds &sounds);

    void tick();

private:
    void playMachineSounds();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
};
