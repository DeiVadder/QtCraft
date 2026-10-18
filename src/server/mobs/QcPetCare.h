#pragma once

#include <QString>

class QcContentRegistry;
class QcEntities;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcCreature;
struct QcItemDefinition;
struct QcMobDefinition;
struct QcServerClient;

// Players using items on tameable creatures (decision E-480): a taming item tames a wild one with its
// chance (a wolf becomes a dog; the pet sits at first and belongs to the player's identity,
// QcPetOwners); on a tame one, healing food heals it, a dye colours its collar, and anything else (an
// empty hand too) makes it sit or stand up, all only for its owner except healing.
class QcPetCare
{
public:
    QcPetCare(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Uses the held item (or the empty hand) on the creature; false when this is no use of a tameable
    // creature (the use goes on to feeding, catching or eating).
    bool use(QcServerClient &client, quint32 mobId);
    // Makes the creature the pet of the owner key: its tame kind (QcTameDefinition::becomes), sitting,
    // its kind's collar, kept with the world. World setups (the showcase) tame with a made-up key.
    static void tame(QcEntities &entities, const QcContentRegistry &content, QcCreature &mob, const QString &owner);

private:
    bool tend(QcServerClient &client, QcCreature &mob, const QcMobDefinition &definition, const QcItemDefinition *held);
    // Uses up one of the held item (not in creative mode) and tells the client.
    void consume(QcServerClient &client);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
