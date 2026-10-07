#pragma once

#include <QObject>

// Authoritative game server. Owns the world and all game logic.
class QcServer : public QObject
{
    Q_OBJECT

public:
    explicit QcServer(QObject *parent = nullptr);
};
