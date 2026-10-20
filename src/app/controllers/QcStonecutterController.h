#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// The stonecutter screen for QML (GameController.stonecutter, decision E-785): the stone blocks of
// the inventory the stonecutter cuts (with how many the inventory holds), the selected one and what
// it becomes by each of its stonecutting recipes. cut() asks the server; the inventory comes back
// from it.
class QcStonecutterController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool open READ isOpen NOTIFY changed FINAL)
    // One map per input: icon, count (held in the inventory), label.
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed FINAL)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY changed FINAL)
    // One map per result of the selected input: icon, count (per input block), label.
    Q_PROPERTY(QVariantList results READ results NOTIFY changed FINAL)

public:
    QcStonecutterController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    bool isOpen() const;
    QVariantList inputs() const { return m_inputs; }
    QVariantList results() const { return m_results; }
    int selected() const { return m_selected; }
    void select(int index);

    // Cuts one input block into the result at `index`, or as many as possible with `repeat`
    // (shift-click).
    Q_INVOKABLE void cut(int index, bool repeat) const;

    // The screen opened or closed or the inventory changed: inputs and results again, the selected
    // input kept while the inventory still holds it.
    void update();

signals:
    void changed();

private:
    QVariantMap stackMap(const QString &item, int count) const;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    QStringList m_inputNames;
    QStringList m_recipes;
    QVariantList m_inputs;
    QVariantList m_results;
    int m_selected = 0;
};
