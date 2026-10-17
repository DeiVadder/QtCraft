#pragma once

#include "clientcore/settings/QcCharacterPresets.h"
#include "clientcore/settings/QcCharacterProfile.h"
#include "common/content/definitions/QcAppearance.h"
#include "common/content/definitions/QcAppearanceDefinition.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QcContentRegistry;
class QcSettingsController;
class QcWorldLibraryController;
struct QcSessionParts;

// The player's look (GameController.settings.character, stored with the settings), the outfits,
// hair styles and capes the content offers for it, the colours to choose from and the presets: built-in
// ones and those the player saved (decision E-231). Every change applies at once, also to the
// player of a running world.
class QcCharacterSettings : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // {outfit, hair, skin, hairColor, shirt, pants, cape} with "#rrggbb" colours.
    Q_PROPERTY(QVariantMap look READ look WRITE setLook NOTIFY lookChanged FINAL)
    // Outfits and hair styles of the content as [{name, label}].
    Q_PROPERTY(QVariantList outfits READ outfits NOTIFY appearancesChanged FINAL)
    Q_PROPERTY(QVariantList hairStyles READ hairStyles NOTIFY appearancesChanged FINAL)
    // Capes of the content as [{name, label}], "No cape" (empty name) first.
    Q_PROPERTY(QVariantList capes READ capes NOTIFY appearancesChanged FINAL)
    // Colours offered per part as "#rrggbb".
    Q_PROPERTY(QStringList skinColors READ skinColors CONSTANT FINAL)
    Q_PROPERTY(QStringList hairColors READ hairColors CONSTANT FINAL)
    Q_PROPERTY(QStringList clothColors READ clothColors CONSTANT FINAL)
    // Built-in presets first, then the player's: [{label, builtIn, look}].
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged FINAL)
    Q_PROPERTY(bool canSavePreset READ canSavePreset NOTIFY presetsChanged FINAL)
    // Places for the player's own presets.
    Q_PROPERTY(int maxPresets READ maxPresets CONSTANT FINAL)

public:
    QcCharacterSettings(const QcSessionParts &parts, const QcWorldLibraryController &library, const QcSettingsController &settings,
                        QObject *parent = nullptr);

    QVariantMap look() const;
    // Applies at once, also to the player of a running world.
    void setLook(const QVariantMap &look);
    QVariantList outfits() const;
    QVariantList hairStyles() const;
    QVariantList capes() const;
    static QStringList skinColors();
    static QStringList hairColors();
    static QStringList clothColors();
    QVariantList presets() const { return m_presets.rows(); }
    bool canSavePreset() const { return m_presets.canSave(); }
    static int maxPresets() { return QcCharacterProfile::maxPresets; }

    // A random look from the content's outfits and hair styles and the offered colours.
    Q_INVOKABLE void randomize();
    // The default look.
    Q_INVOKABLE void reset();
    // Takes the look of a row of `presets`.
    Q_INVOKABLE void applyPreset(int row);
    // Saves the current look as the player's preset `name` (replacing one of that name); false
    // when the name is empty or all preset places are taken.
    Q_INVOKABLE bool savePreset(const QString &name);
    // Whether savePreset(name) would succeed.
    Q_INVOKABLE bool canSavePresetAs(const QString &name) const { return m_presets.canSaveAs(name); }
    // `text` cut to the length of a preset name (between graphemes), for the name field.
    Q_INVOKABLE QString fitPresetName(const QString &text) const;
    // Removes a preset of the player (row of `presets`); built-in ones stay.
    Q_INVOKABLE void removePreset(int row);

    const QcAppearance &appearance() const { return m_appearance; }
    void setAppearance(const QcAppearance &appearance);
    const QList<QcCharacterPreset> &savedPresets() const { return m_presets.saved(); }
    // The stored look and presets; signals the changes so bindings pick them up.
    void load(const QcAppearance &appearance, const QList<QcCharacterPreset> &presets);
    // The lists changed with the content or the language (the presets follow the language on
    // their own, QcCharacterPresets).
    void refreshLists();

signals:
    void lookChanged();
    void appearancesChanged();
    void presetsChanged();

private:
    // The running world's content, else base game plus enabled mods loaded on demand.
    const QcContentRegistry &content() const;
    QVariantList appearanceList(QcAppearanceDefinition::Kind kind) const;
    QStringList playableNames(QcAppearanceDefinition::Kind kind) const;

    const QcSessionParts &m_parts;
    const QcWorldLibraryController &m_library;
    const QcSettingsController &m_settings;
    QcAppearance m_appearance;
    QcCharacterPresets m_presets;
};
