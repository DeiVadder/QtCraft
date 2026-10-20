#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// The stonecutter screen for QML (GameController.stonecutter, decision E-785): what the stone in the
// input slot becomes by each of its stonecutting recipes, and which one is chosen. select() asks the
// server, which sends the slots back; the input and output slots themselves are rows of the
// inventory model (GameController.player.inventory).
class QcStonecutterController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool open READ isOpen NOTIFY changed FINAL)
    // One map per result of the input: icon, count (per input item), label.
    Q_PROPERTY(QVariantList results READ results NOTIFY changed FINAL)
    // The chosen result's index in `results`; -1 for none.
    Q_PROPERTY(int selected READ selected NOTIFY changed FINAL)

public:
    QcStonecutterController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    bool isOpen() const;
    QVariantList results() const { return m_results; }
    int selected() const { return m_selected; }

    // Chooses the result at `index` for the input (the output slot shows it once the server agrees).
    Q_INVOKABLE void select(int index) const;

    // The screen opened, changed or closed: the results again.
    void update();

signals:
    void changed();

private:
    QVariantMap stackMap(const QString &item, int count) const;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    QStringList m_recipes;
    QVariantList m_results;
    int m_selected = -1;
};
