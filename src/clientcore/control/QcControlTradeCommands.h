#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands for trading with villagers (decision E-414): opening a villager's screen,
// listing its offers, trading and closing.
class QcControlTradeCommands
{
public:
    explicit QcControlTradeCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void trades(const QJsonObject &request);
    void openTrade(const QJsonObject &request);
    void trade(const QJsonObject &request);
    void closeTrade(const QJsonObject &request);
    // The open screen: villager, profession, level, experience and the offers with their state.
    QJsonObject tradesJson() const;

    QcControlContext &m_context;
};
