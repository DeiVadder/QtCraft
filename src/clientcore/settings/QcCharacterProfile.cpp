#include "QcCharacterProfile.h"

#include "QcNameText.h"

#include <QRandomGenerator>
#include <QSettings>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace QcCharacterProfile {

namespace {

const QString presetsGroup = u"characterPresets"_s;

quint32 readColor(const QSettings &file, const QString &key, quint32 fallback)
{
    const QString text = file.value(key).toString();
    bool ok = false;
    const quint32 rgb = text.startsWith(u'#') && text.size() == 7 ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
    return ok ? rgb : fallback;
}

QString readName(const QSettings &file, const QString &key, const QString &fallback)
{
    const QVariant value = file.value(key);
    return value.isValid() && value.toString().size() <= QcAppearance::maxNameLength ? value.toString() : fallback;
}

QcAppearance look(const char *outfit, const char *hair, std::array<quint32, 4> colors)
{
    return {QString::fromLatin1(outfit), QString::fromLatin1(hair), colors[0], colors[1], colors[2], colors[3]};
}

} // namespace

const QList<quint32> &skinColors()
{
    static const QList<quint32> colors{0xf3d2b3, 0xe8b48f, 0xd8a07a, 0xb97b56, 0x8d5a3b, 0x5c3a24};
    return colors;
}

const QList<quint32> &hairColors()
{
    static const QList<quint32> colors{0xf1d77a, 0xc99a4e, 0x8a5a2b, 0x4a3020, 0x1e1a18, 0xb23a24, 0x9a9a9a};
    return colors;
}

const QList<quint32> &clothColors()
{
    static const QList<quint32> colors{0x3a7bc8, 0x2f8f4e, 0xc83a3a, 0xd8a030, 0x7a4ab8, 0xe8e8e8, 0x34406a, 0x3a3a3a, 0x8a5a2b};
    return colors;
}

const QList<QcCharacterPreset> &builtInPresets()
{
    static const QList<QcCharacterPreset> presets{
        {QString::fromLatin1(QT_TRANSLATE_NOOP("QcCharacterProfile", "Explorer")), QcAppearance()},
        {QString::fromLatin1(QT_TRANSLATE_NOOP("QcCharacterProfile", "Miner")), look("qtcraft:miner", "qtcraft:cap", {0xe8b48f, 0x1e1a18, 0xd8a030, 0x3a3a3a})},
        {QString::fromLatin1(QT_TRANSLATE_NOOP("QcCharacterProfile", "Traveller")),
         look("qtcraft:tunic", "qtcraft:long_hair", {0xb97b56, 0xb23a24, 0x2f8f4e, 0x8a5a2b})},
        {QString::fromLatin1(QT_TRANSLATE_NOOP("QcCharacterProfile", "Night owl")),
         look("qtcraft:explorer", "qtcraft:long_hair", {0x5c3a24, 0x1e1a18, 0x7a4ab8, 0x3a3a3a})},
    };
    return presets;
}

QcAppearance random(const QStringList &outfits, const QStringList &hairStyles, quint32 seed)
{
    QRandomGenerator generator(seed);
    const auto pick = [&generator](const QList<quint32> &colors) { return colors[qsizetype(generator.bounded(quint32(colors.size())))]; };

    QcAppearance appearance;
    if (!outfits.isEmpty()) {
        appearance.outfit = outfits[qsizetype(generator.bounded(quint32(outfits.size())))];
    }
    // One more choice than there are hair styles: no hair.
    const auto hair = qsizetype(generator.bounded(quint32(hairStyles.size() + 1)));
    appearance.hair = hair < hairStyles.size() ? hairStyles[hair] : QString();
    appearance.skinColor = pick(skinColors());
    appearance.hairColor = pick(hairColors());
    appearance.shirtColor = pick(clothColors());
    appearance.pantsColor = pick(clothColors());
    return appearance;
}

QString cleanPresetName(const QString &name)
{
    return QcNameText::clean(name, maxPresetNameLength);
}

QString hex(quint32 rgb)
{
    return u"#%1"_s.arg(uint(rgb & 0xFFFFFFu), 6, 16, QChar(u'0'));
}

QVariantMap lookMap(const QcAppearance &appearance)
{
    return {{u"outfit"_s, appearance.outfit},         {u"hair"_s, appearance.hair},
            {u"skin"_s, hex(appearance.skinColor)},   {u"hairColor"_s, hex(appearance.hairColor)},
            {u"shirt"_s, hex(appearance.shirtColor)}, {u"pants"_s, hex(appearance.pantsColor)}};
}

QcAppearance loadAppearance(const QSettings &file, const QString &group)
{
    const QcAppearance defaults;
    const QString prefix = group + u'/';
    QcAppearance appearance;
    appearance.outfit = readName(file, prefix + u"outfit"_s, defaults.outfit);
    appearance.hair = readName(file, prefix + u"hair"_s, defaults.hair);
    appearance.skinColor = readColor(file, prefix + u"skin"_s, defaults.skinColor);
    appearance.hairColor = readColor(file, prefix + u"hairColor"_s, defaults.hairColor);
    appearance.shirtColor = readColor(file, prefix + u"shirt"_s, defaults.shirtColor);
    appearance.pantsColor = readColor(file, prefix + u"pants"_s, defaults.pantsColor);
    return appearance;
}

void saveAppearance(QSettings &file, const QString &group, const QcAppearance &appearance)
{
    const QString prefix = group + u'/';
    file.setValue(prefix + u"outfit"_s, appearance.outfit);
    file.setValue(prefix + u"hair"_s, appearance.hair);
    file.setValue(prefix + u"skin"_s, hex(appearance.skinColor));
    file.setValue(prefix + u"hairColor"_s, hex(appearance.hairColor));
    file.setValue(prefix + u"shirt"_s, hex(appearance.shirtColor));
    file.setValue(prefix + u"pants"_s, hex(appearance.pantsColor));
}

QList<QcCharacterPreset> loadPresets(const QSettings &file)
{
    QList<QcCharacterPreset> presets;
    const int count = std::clamp(file.value(presetsGroup + u"/size"_s).toInt(), 0, maxPresets);
    for (int index = 0; index < count; ++index) {
        const QString group = u"%1/%2"_s.arg(presetsGroup).arg(index + 1);
        const QString name = cleanPresetName(file.value(group + u"/name"_s).toString());
        if (!name.isEmpty()) {
            presets.append({name, loadAppearance(file, group)});
        }
    }
    return presets;
}

void savePresets(QSettings &file, const QList<QcCharacterPreset> &presets)
{
    file.remove(presetsGroup);
    const qsizetype count = std::min<qsizetype>(presets.size(), maxPresets);
    file.setValue(presetsGroup + u"/size"_s, count);
    for (qsizetype index = 0; index < count; ++index) {
        const QString group = u"%1/%2"_s.arg(presetsGroup).arg(index + 1);
        file.setValue(group + u"/name"_s, cleanPresetName(presets[index].name));
        saveAppearance(file, group, presets[index].appearance);
    }
}

} // namespace QcCharacterProfile
