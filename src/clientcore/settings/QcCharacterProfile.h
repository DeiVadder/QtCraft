#pragma once

#include "common/content/definitions/QcAppearance.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QSettings;

// A look saved under a name: a built-in preset or one the player saved.
struct QcCharacterPreset
{
    QString name;
    QcAppearance appearance;

    bool operator==(const QcCharacterPreset &) const = default;
};

// The player's character profile apart from the look itself: the colours the character screen
// offers, the built-in presets, random looks, and how look and presets are stored in settings.ini
// (decision E-231). GUI-free; the server checks every look again (QcProfileValidation).
namespace QcCharacterProfile {

inline constexpr int maxPresets = 8;
// In Unicode code points (QcNameText).
inline constexpr qsizetype maxPresetNameLength = 32;

// Colours offered for each part as 0xRRGGBB; the settings file may hold any other colour.
const QList<quint32> &skinColors();
const QList<quint32> &hairColors();
const QList<quint32> &clothColors();

// Presets of the base game; their names are English texts translated in the context
// "QcCharacterProfile".
const QList<QcCharacterPreset> &builtInPresets();

// A random look: an outfit of `outfits` (the default outfit when empty), a hair style of
// `hairStyles` or none, a cape of `capes` or none, and colours from the offered ones. The same seed
// gives the same look.
QcAppearance random(const QStringList &outfits, const QStringList &hairStyles, const QStringList &capes, quint32 seed);

// Without unpaired surrogates, control and format characters, trimmed, at most
// maxPresetNameLength code points cut between graphemes (QcNameText::clean).
QString cleanPresetName(const QString &name);

// {outfit, hair, skin, hairColor, shirt, pants, cape} with "#rrggbb" colours, as QML sees a look.
QVariantMap lookMap(const QcAppearance &appearance);
// "#rrggbb".
QString hex(quint32 rgb);

// The look in `group` ("character"); names longer than QcAppearance::maxNameLength and colours
// that are not "#rrggbb" fall back to the default look's.
QcAppearance loadAppearance(const QSettings &file, const QString &group);
void saveAppearance(QSettings &file, const QString &group, const QcAppearance &appearance);
// The player's presets ("characterPresets/<n>/..."); at most maxPresets, nameless ones skipped.
QList<QcCharacterPreset> loadPresets(const QSettings &file);
void savePresets(QSettings &file, const QList<QcCharacterPreset> &presets);

} // namespace QcCharacterProfile
