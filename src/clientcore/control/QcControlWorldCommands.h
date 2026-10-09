#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands that query blocks and act on them: block queries, mining and using the held item on a block.
class QcControlWorldCommands
{
public:
    explicit QcControlWorldCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void block(const QJsonObject &request);
    void blocks(const QJsonObject &request);
    void mine(const QJsonObject &request);
    // `place` and `use`.
    void useOnBlock(const QJsonObject &request);

    QcControlContext &m_context;
};
