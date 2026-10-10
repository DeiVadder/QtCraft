#include "QcCharacterSettings.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcNameText.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"
#include "QcWorldLibraryController.h"

#include <QRandomGenerator>

using namespace Qt::StringLiterals;

namespace {

QStringList hexList(const QList<quint32> &colors)
{
    QStringList list;
    for (const quint32 color : colors) {
        list.append(QcCharacterProfile::hex(color));
    }
    return list;
}

} // namespace

QcCharacterSettings::QcCharacterSettings(const QcSessionParts &parts, const QcWorldLibraryController &library,
                                         const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_library(library)
    , m_settings(settings)
{
    connect(&m_presets, &QcCharacterPresets::changed, this, &QcCharacterSettings::presetsChanged);
}

QVariantMap QcCharacterSettings::look() const
{
    return QcCharacterProfile::lookMap(m_appearance);
}

void QcCharacterSettings::setLook(const QVariantMap &look)
{
    QcAppearance appearance = m_appearance;
    const auto color = [&look](const QString &key, quint32 fallback) {
        const QString text = look.value(key).toString();
        bool ok = false;
        const quint32 rgb = text.startsWith(u'#') && text.size() == 7 ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
        return ok ? rgb : fallback;
    };
    appearance.outfit = look.value(u"outfit"_s, appearance.outfit).toString().left(QcAppearance::maxNameLength);
    appearance.hair = look.value(u"hair"_s, appearance.hair).toString().left(QcAppearance::maxNameLength);
    appearance.skinColor = color(u"skin"_s, appearance.skinColor);
    appearance.hairColor = color(u"hairColor"_s, appearance.hairColor);
    appearance.shirtColor = color(u"shirt"_s, appearance.shirtColor);
    appearance.pantsColor = color(u"pants"_s, appearance.pantsColor);
    setAppearance(appearance);
}

void QcCharacterSettings::setAppearance(const QcAppearance &appearance)
{
    if (appearance == m_appearance) {
        return;
    }

    m_appearance = appearance;
    if (m_parts.session) {
        m_parts.session->setAppearance(m_appearance);
    }
    emit lookChanged();
}

void QcCharacterSettings::load(const QcAppearance &appearance, const QList<QcCharacterPreset> &presets)
{
    m_appearance = appearance;
    emit lookChanged();
    m_presets.load(presets);
}

const QcContentRegistry &QcCharacterSettings::content() const
{
    if (m_parts.session && m_parts.session->content()) {
        return *m_parts.session->content();
    }
    return m_library.content();
}

QVariantList QcCharacterSettings::appearanceList(QcAppearanceDefinition::Kind kind) const
{
    QVariantList list;
    const QcContentRegistry &content = this->content();
    for (const QcAppearanceDefinition &definition : content.appearances()) {
        if (definition.kind == kind && definition.playable) {
            list.append(QVariantMap{{u"name"_s, definition.name},
                                    {u"label"_s, content.translate(definition.translationKey, m_settings.contentLanguage())}});
        }
    }
    return list;
}

QStringList QcCharacterSettings::playableNames(QcAppearanceDefinition::Kind kind) const
{
    QStringList names;
    for (const QcAppearanceDefinition &definition : content().appearances()) {
        if (definition.kind == kind && definition.playable) {
            names.append(definition.name);
        }
    }
    return names;
}

QVariantList QcCharacterSettings::outfits() const
{
    return appearanceList(QcAppearanceDefinition::Kind::Outfit);
}

QVariantList QcCharacterSettings::hairStyles() const
{
    QVariantList list = appearanceList(QcAppearanceDefinition::Kind::Hair);
    list.prepend(QVariantMap{{u"name"_s, QString()}, {u"label"_s, tr("No hair")}});
    return list;
}

QStringList QcCharacterSettings::skinColors()
{
    return hexList(QcCharacterProfile::skinColors());
}

QStringList QcCharacterSettings::hairColors()
{
    return hexList(QcCharacterProfile::hairColors());
}

QStringList QcCharacterSettings::clothColors()
{
    return hexList(QcCharacterProfile::clothColors());
}

void QcCharacterSettings::randomize()
{
    setAppearance(QcCharacterProfile::random(playableNames(QcAppearanceDefinition::Kind::Outfit),
                                             playableNames(QcAppearanceDefinition::Kind::Hair), QRandomGenerator::global()->generate()));
}

void QcCharacterSettings::reset()
{
    setAppearance(QcAppearance());
}

void QcCharacterSettings::applyPreset(int row)
{
    if (const std::optional<QcAppearance> look = m_presets.appearance(row)) {
        setAppearance(*look);
    }
}

bool QcCharacterSettings::savePreset(const QString &name)
{
    return m_presets.save(name, m_appearance);
}

QString QcCharacterSettings::fitPresetName(const QString &text) const
{
    return QcNameText::fit(text, QcCharacterProfile::maxPresetNameLength);
}

void QcCharacterSettings::removePreset(int row)
{
    m_presets.remove(row);
}

void QcCharacterSettings::refreshLists()
{
    emit appearancesChanged();
}
