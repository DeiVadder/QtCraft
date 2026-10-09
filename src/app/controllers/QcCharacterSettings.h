#pragma once

#include "QcAppearance.h"
#include "QcAppearanceDefinition.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QcContentRegistry;
class QcSettingsController;
class QcWorldLibraryController;
struct QcSessionParts;

// The player's look (GameController.settings.character, stored with the settings) and the outfits
// and hair styles the content offers for it.
class QcCharacterSettings : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // {outfit, hair, skin, hairColor, shirt, pants} with "#rrggbb" colours.
    Q_PROPERTY(QVariantMap look READ look WRITE setLook NOTIFY lookChanged FINAL)
    // Outfits and hair styles of the content as [{name, label}].
    Q_PROPERTY(QVariantList outfits READ outfits NOTIFY appearancesChanged FINAL)
    Q_PROPERTY(QVariantList hairStyles READ hairStyles NOTIFY appearancesChanged FINAL)

public:
    QcCharacterSettings(const QcSessionParts &parts, const QcWorldLibraryController &library, const QcSettingsController &settings,
                        QObject *parent = nullptr);

    QVariantMap look() const;
    // Applies at once, also to the player of a running world.
    void setLook(const QVariantMap &look);
    QVariantList outfits() const;
    QVariantList hairStyles() const;

    const QcAppearance &appearance() const { return m_appearance; }
    // The stored look; signals lookChanged so bindings pick it up.
    void load(const QcAppearance &appearance)
    {
        m_appearance = appearance;
        emit lookChanged();
    }
    // The lists changed with the content or the language.
    void refreshLists();

signals:
    void lookChanged();
    void appearancesChanged();

private:
    // The running world's content, else base game plus enabled mods loaded on demand.
    const QcContentRegistry &content() const;
    QVariantList appearanceList(QcAppearanceDefinition::Kind kind) const;

    const QcSessionParts &m_parts;
    const QcWorldLibraryController &m_library;
    const QcSettingsController &m_settings;
    QcAppearance m_appearance;
};
