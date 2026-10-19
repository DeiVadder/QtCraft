#pragma once

#include "clientcore/input/QcGamepadGlyphs.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QcGamepadSource;

// The connected pads for QML (GameController.gamepad.pads, decisions E-612 and E-613): their list,
// the one in use (the pad that last pressed something or clearly pushed a stick; at first the first
// connected one), the family of its button labels, and a notice when a pad connects, disconnects or
// another pad takes over. Hints draw the buttons with glyph() and name them in help texts with
// `texts` (QcGamepadGlyphs). Follows the source's signals only: nothing per frame.
class QcGamepadRoster : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // Every connected pad: {id, name, type, family, guid, inUse, rumble}, in the order they connected.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged FINAL)
    // "xbox", "playstation", "nintendo" or "generic" of the pad in use (Xbox without a pad).
    Q_PROPERTY(QString family READ family NOTIFY familyChanged FINAL)
    // The name of the pad in use (empty without a pad) and its model (empty when unknown).
    Q_PROPERTY(QString name READ name NOTIFY listChanged FINAL)
    Q_PROPERTY(QString guid READ guid NOTIFY listChanged FINAL)
    // The inputs in running text for the family in use: texts.south is "A", "✕", "B" or "bottom button".
    Q_PROPERTY(QVariantMap texts READ texts NOTIFY familyChanged FINAL)
    // The last notice (connected, disconnected, in use) and a counter that rises with every one, so
    // the same text twice is shown twice.
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged FINAL)
    Q_PROPERTY(int noticeCount READ noticeCount NOTIFY noticeChanged FINAL)

public:
    explicit QcGamepadRoster(QObject *parent = nullptr);
    ~QcGamepadRoster() override;

    void setSource(QcGamepadSource *source);

    QVariantList list() const;
    QString family() const { return QcGamepadGlyphs::familyName(m_family); }
    QcGamepadFamily familyValue() const { return m_family; }
    QString name() const;
    QString guid() const;
    QVariantMap texts() const;
    QString notice() const { return m_notice; }
    int noticeCount() const { return m_noticeCount; }
    // The pad in use (0: none).
    int inUse() const { return m_inUse; }

    // The glyph of `input` in `family`: {shape, label, colour, direction}; empty for an unknown name.
    Q_INVOKABLE QVariantMap glyph(const QString &family, const QString &input) const;
    // `input` in running text for the family in use.
    Q_INVOKABLE QString text(const QString &input) const;

signals:
    void listChanged();
    void familyChanged();
    void noticeChanged();

private:
    void onConnected(int id);
    void onDisconnected(int id);
    void onStateChanged(int id);
    // Pad `id` is in use (0: none); `announce` gives a notice when it takes over from another.
    void use(int id, bool announce);
    void announce(const QString &text);

    QPointer<QcGamepadSource> m_source;
    // Names of the connected pads, for the notice when one is gone (the source forgot it then).
    QHash<int, QString> m_names;
    int m_inUse = 0;
    QcGamepadFamily m_family = QcGamepadFamily::Xbox;
    QString m_notice;
    int m_noticeCount = 0;
};
