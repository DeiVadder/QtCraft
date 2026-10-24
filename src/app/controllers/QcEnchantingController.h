#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// The enchanting table's screen for QML (GameController.enchanting, decision E-864): the three
// offers with their levels, prices and clues, the bookshelves that count, and choose(). The item
// and lapis slots themselves are rows of the inventory model (GameController.player.inventory);
// the server decides and sends everything back.
class QcEnchantingController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool open READ isOpen NOTIFY changed FINAL)
    // Bookshelves around the table that count (0–15).
    Q_PROPERTY(int shelves READ shelves NOTIFY changed FINAL)
    // One map per offer: levelCost (0: none), price (levels and lapis it takes), signs (the first
    // enchantment by name as runes and readable characters, QcRuneScript: a list of maps with rune,
    // -1 for a readable character, and text), letters and readableLetters (how many letters, not
    // counting spaces, and how many of them are readable), clue (the name in plain text once all
    // letters are readable, else empty), levelsLacking and lapisLacking (why it cannot be taken),
    // affordable (the player can take it now).
    Q_PROPERTY(QVariantList offers READ offers NOTIFY changed FINAL)

public:
    QcEnchantingController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    bool isOpen() const;
    int shelves() const { return m_shelves; }
    QVariantList offers() const { return m_offers; }

    // Takes the offer at `index` (the slots change once the server agrees).
    Q_INVOKABLE void choose(int index) const;
    // The screen, the player's level or the inventory changed: the offers again.
    void update();

signals:
    void changed();

private:
    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    int m_shelves = 0;
    QVariantList m_offers;
};
