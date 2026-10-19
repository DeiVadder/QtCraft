#include "app/input/QcGamepadRoster.h"

#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/input/QcGamepadSource.h"

using namespace Qt::StringLiterals;

namespace {

// Every input that has a text (QcGamepadGlyphs): the buttons, the triggers and the tilted sticks.
const QStringList &inputNames()
{
    static const QStringList names = [] {
        QStringList result;
        for (std::size_t button = 0; button < qcGamepadButtonCount; ++button) {
            result.append(QcGamepadNames::button(QcGamepadButton(button)));
        }
        result << u"leftTrigger"_s << u"rightTrigger"_s << u"leftStickMove"_s << u"rightStickMove"_s;
        return result;
    }();
    return names;
}

} // namespace

QcGamepadRoster::QcGamepadRoster(QObject *parent)
    : QObject(parent)
{
}

QcGamepadRoster::~QcGamepadRoster() = default;

void QcGamepadRoster::setSource(QcGamepadSource *source)
{
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    m_names.clear();
    if (m_source) {
        connect(m_source, &QcGamepadSource::padConnected, this, &QcGamepadRoster::onConnected);
        connect(m_source, &QcGamepadSource::padDisconnected, this, &QcGamepadRoster::onDisconnected);
        connect(m_source, &QcGamepadSource::stateChanged, this, &QcGamepadRoster::onStateChanged);
        const QList<QcGamepadInfo> pads = m_source->pads();
        for (const QcGamepadInfo &pad : pads) {
            m_names.insert(pad.id, pad.name);
        }
    }

    const QList<QcGamepadInfo> pads = m_source ? m_source->pads() : QList<QcGamepadInfo>();
    use(pads.isEmpty() ? 0 : pads.first().id, false);
    emit listChanged();
}

QVariantList QcGamepadRoster::list() const
{
    QVariantList result;
    if (!m_source) {
        return result;
    }
    const QList<QcGamepadInfo> pads = m_source->pads();
    for (const QcGamepadInfo &pad : pads) {
        result.append(QVariantMap{
            {u"id"_s, pad.id},
            {u"name"_s, pad.name},
            {u"type"_s, QcGamepadNames::type(pad.type)},
            {u"family"_s, QcGamepadGlyphs::familyName(QcGamepadGlyphs::family(pad.type))},
            {u"guid"_s, pad.guid},
            {u"inUse"_s, pad.id == m_inUse},
            {u"rumble"_s, pad.rumble},
        });
    }
    return result;
}

QString QcGamepadRoster::name() const
{
    return m_names.value(m_inUse);
}

QString QcGamepadRoster::guid() const
{
    const std::optional<QcGamepadInfo> pad = m_source ? m_source->info(m_inUse) : std::nullopt;
    return pad ? pad->guid : QString();
}

QVariantMap QcGamepadRoster::texts() const
{
    QVariantMap result;
    for (const QString &input : inputNames()) {
        result.insert(input, text(input));
    }
    return result;
}

QVariantMap QcGamepadRoster::glyph(const QString &family, const QString &input) const
{
    const std::optional<QcGamepadFamily> parsedFamily = QcGamepadGlyphs::familyFromName(family);
    const std::optional<QcGamepadGlyphs::Glyph> found = QcGamepadGlyphs::glyph(parsedFamily.value_or(m_family), input);
    if (!found) {
        return {};
    }
    return {{u"shape"_s, QcGamepadGlyphs::shapeName(found->shape)},
            {u"label"_s, found->label},
            {u"colour"_s, found->colour},
            {u"direction"_s, found->direction}};
}

QString QcGamepadRoster::text(const QString &input) const
{
    if (input == u"leftStickMove"_s) {
        return tr("Left stick");
    }
    if (input == u"rightStickMove"_s) {
        return tr("Right stick");
    }

    QString label = QcGamepadGlyphs::text(m_family, input);
    if (!label.isEmpty() || m_family != QcGamepadFamily::Generic) {
        return label;
    }
    // The face buttons of a pad of unknown make, by their place.
    if (input == u"south"_s) {
        return tr("Bottom button");
    }
    if (input == u"east"_s) {
        return tr("Right button");
    }
    if (input == u"west"_s) {
        return tr("Left button");
    }
    return input == u"north"_s ? tr("Top button") : QString();
}

void QcGamepadRoster::onConnected(int id)
{
    const std::optional<QcGamepadInfo> pad = m_source->info(id);
    m_names.insert(id, pad ? pad->name : QString());
    announce(tr("Controller connected: %1").arg(m_names.value(id)));
    if (m_inUse == 0) {
        use(id, false);
    }
    emit listChanged();
}

void QcGamepadRoster::onDisconnected(int id)
{
    announce(tr("Controller disconnected: %1").arg(m_names.take(id)));
    if (id == m_inUse) {
        const QList<QcGamepadInfo> pads = m_source->pads();
        use(pads.isEmpty() ? 0 : pads.first().id, false);
    }
    emit listChanged();
}

void QcGamepadRoster::onStateChanged(int id)
{
    if (id != m_inUse && QcGamepadMapping::used(m_source->state(id))) {
        use(id, true);
        emit listChanged();
    }
}

void QcGamepadRoster::use(int id, bool announceIt)
{
    const bool takesOver = announceIt && m_inUse != 0 && id != 0 && id != m_inUse;
    m_inUse = id;
    if (takesOver) {
        announce(tr("Controller in use: %1").arg(m_names.value(id)));
    }

    // Without a pad the hints keep the last family (Xbox at first).
    const std::optional<QcGamepadInfo> pad = m_source && id != 0 ? m_source->info(id) : std::nullopt;
    const QcGamepadFamily family = pad ? QcGamepadGlyphs::family(pad->type) : m_family;
    if (family != m_family) {
        m_family = family;
        emit familyChanged();
    }
}

void QcGamepadRoster::announce(const QString &text)
{
    m_notice = text;
    ++m_noticeCount;
    emit noticeChanged();
}
