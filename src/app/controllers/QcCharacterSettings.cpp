#include "QcCharacterSettings.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"
#include "QcWorldLibraryController.h"

using namespace Qt::StringLiterals;

QcCharacterSettings::QcCharacterSettings(const QcSessionParts &parts, const QcWorldLibraryController &library,
                                         const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_library(library)
    , m_settings(settings)
{
}

QVariantMap QcCharacterSettings::look() const
{
    const auto hex = [](quint32 rgb) { return u"#%1"_s.arg(uint(rgb & 0xFFFFFFu), 6, 16, QChar(u'0')); };
    return {{u"outfit"_s, m_appearance.outfit},          {u"hair"_s, m_appearance.hair},
            {u"skin"_s, hex(m_appearance.skinColor)},    {u"hairColor"_s, hex(m_appearance.hairColor)},
            {u"shirt"_s, hex(m_appearance.shirtColor)},  {u"pants"_s, hex(m_appearance.pantsColor)}};
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
    if (appearance == m_appearance) {
        return;
    }

    m_appearance = appearance;
    if (m_parts.session) {
        m_parts.session->setAppearance(m_appearance);
    }
    emit lookChanged();
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

void QcCharacterSettings::refreshLists()
{
    emit appearancesChanged();
}
