#include "app/controllers/QcCharacterSettings.h"

#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcNameText.h"

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
    return QcCharacterProfile::lookMap(shownAppearance());
}

void QcCharacterSettings::setLook(const QVariantMap &look)
{
    QcAppearance appearance = shownAppearance();
    const auto color = [&look](const QString &key, quint32 fallback) {
        const QString text = look.value(key).toString();
        bool ok = false;
        const quint32 rgb = text.startsWith(u'#') && text.size() == 7 ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
        // A missing key keeps the colour on purpose (the screen sends only what changed).
        if (!ok && look.contains(key)) {
            qCWarning(lcGame) << "Character look" << key << ": ignoring" << text << "(expected #rrggbb)";
        }
        return ok ? rgb : fallback;
    };
    appearance.outfit = look.value(u"outfit"_s, appearance.outfit).toString().left(QcAppearance::maxNameLength);
    appearance.hair = look.value(u"hair"_s, appearance.hair).toString().left(QcAppearance::maxNameLength);
    appearance.skinColor = color(u"skin"_s, appearance.skinColor);
    appearance.hairColor = color(u"hairColor"_s, appearance.hairColor);
    appearance.shirtColor = color(u"shirt"_s, appearance.shirtColor);
    appearance.pantsColor = color(u"pants"_s, appearance.pantsColor);
    appearance.cape = look.value(u"cape"_s, appearance.cape).toString().left(QcAppearance::maxNameLength);
    change(appearance);
}

void QcCharacterSettings::change(const QcAppearance &appearance)
{
    if (!m_drafting) {
        setAppearance(appearance);
        return;
    }
    if (appearance == m_draft) {
        return;
    }

    m_draft = appearance;
    emit lookChanged();
    emit draftChanged();
}

void QcCharacterSettings::beginDraft()
{
    m_draft = m_appearance;
    m_drafting = true;
    emit lookChanged();
    emit draftChanged();
}

void QcCharacterSettings::saveDraft()
{
    if (m_drafting) {
        setAppearance(m_draft);
        emit draftChanged();
    }
}

void QcCharacterSettings::discardDraft()
{
    if (m_drafting) {
        change(m_appearance);
    }
}

void QcCharacterSettings::endDraft()
{
    if (!m_drafting) {
        return;
    }

    m_drafting = false;
    emit lookChanged();
    emit draftChanged();
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
    // An open draft is compared with the stored look: whether it is unsaved may have changed.
    if (m_drafting) {
        emit draftChanged();
    }
}

void QcCharacterSettings::load(const QcAppearance &appearance, const QList<QcCharacterPreset> &presets)
{
    const bool changed = appearance != m_appearance;
    m_appearance = appearance;
    emit lookChanged();
    if (changed && m_drafting) {
        emit draftChanged();
    }
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
    for (const QcAppearanceDefinition &definition : content.assets().appearances()) {
        if (definition.kind == kind && definition.playable) {
            list.append(QVariantMap{{u"name"_s, definition.name},
                                    {u"label"_s, content.translations().translate(definition.translationKey, m_settings.contentLanguage())}});
        }
    }
    return list;
}

QStringList QcCharacterSettings::playableNames(QcAppearanceDefinition::Kind kind) const
{
    QStringList names;
    for (const QcAppearanceDefinition &definition : content().assets().appearances()) {
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

QVariantList QcCharacterSettings::capes() const
{
    QVariantList list = appearanceList(QcAppearanceDefinition::Kind::Cape);
    list.prepend(QVariantMap{{u"name"_s, QString()}, {u"label"_s, tr("No cape")}});
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
    change(QcCharacterProfile::random(playableNames(QcAppearanceDefinition::Kind::Outfit),
                                             playableNames(QcAppearanceDefinition::Kind::Hair),
                                             playableNames(QcAppearanceDefinition::Kind::Cape), QRandomGenerator::global()->generate()));
}

void QcCharacterSettings::reset()
{
    change(QcAppearance());
}

void QcCharacterSettings::applyPreset(int row)
{
    if (const std::optional<QcAppearance> look = m_presets.appearance(row)) {
        change(*look);
    }
}

bool QcCharacterSettings::savePreset(const QString &name)
{
    return m_presets.save(name, shownAppearance());
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
