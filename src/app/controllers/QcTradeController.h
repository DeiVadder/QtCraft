#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// The trading screen of a villager for QML (GameController.trade, decision E-414): its profession
// and level with the way to the next, its offers (what they cost now, what they give, uses left,
// whether the inventory pays them) and the selected offer, whose costs and result the screen shows
// in large slots. trade() asks the server; the offers come back from it.
class QcTradeController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool open READ isOpen NOTIFY changed FINAL)
    // The profession in the current language and the level's name ("Novice" .. "Master").
    Q_PROPERTY(QString profession READ profession NOTIFY changed FINAL)
    Q_PROPERTY(QString levelName READ levelName NOTIFY changed FINAL)
    Q_PROPERTY(int level READ level NOTIFY changed FINAL)
    // The share of the way to the next level (0..1); 1 at the highest level.
    Q_PROPERTY(double levelProgress READ levelProgress NOTIFY changed FINAL)
    // One map per offer: costs (a list of {icon, count, label}), result ({icon, count, label}),
    // uses, maxUses, soldOut, affordable.
    Q_PROPERTY(QVariantList offers READ offers NOTIFY changed FINAL)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY changed FINAL)

public:
    QcTradeController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    bool isOpen() const;
    QString profession() const;
    QString levelName() const;
    int level() const;
    double levelProgress() const;
    QVariantList offers() const { return m_offers; }
    int selected() const { return m_selected; }
    void select(int index);

    // Trades the offer once, or as often as possible with `repeat` (shift-click).
    Q_INVOKABLE void trade(int index, bool repeat) const;

    // The screen opened, changed or the inventory changed: the offers again.
    void update();
    // A screen of another villager opened: the first offer is selected.
    void reset();

    // The level's name in the current language (1 novice .. 5 master).
    static QString nameOfLevel(int level);

signals:
    void changed();

private:
    QVariantMap stackMap(QcItemStack stack) const;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    QVariantList m_offers;
    int m_selected = 0;
};
