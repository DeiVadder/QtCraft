#include "app/controllers/QcWorldOptionsController.h"

#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QcWorldOptionsController::Unit unitOf(QcWorldOptionUnit unit)
{
    switch (unit) {
    case QcWorldOptionUnit::Blocks:
        return QcWorldOptionsController::Unit::Blocks;
    case QcWorldOptionUnit::Percent:
        return QcWorldOptionsController::Unit::Percent;
    case QcWorldOptionUnit::Factor:
        return QcWorldOptionsController::Unit::Factor;
    case QcWorldOptionUnit::Plain:
        break;
    }
    return QcWorldOptionsController::Unit::Plain;
}

} // namespace

QcWorldOptionsController::QcWorldOptionsController(QcWorldLibraryController &library, std::function<QString()> language, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_language(std::move(language))
{
    // Mods switched: the next form takes the options of the new content.
    connect(&m_library, &QcWorldLibraryController::contentReset, this, &QcWorldOptionsController::release);
}

QcWorldOptionsController::~QcWorldOptionsController() = default;

QString QcWorldOptionsController::preset() const
{
    return content() ? QcWorldOptions::presetOf(chosen(), *content()) : QString();
}

int QcWorldOptionsController::changedCount() const
{
    return static_cast<int>(std::ranges::count_if(options(), [this](const QcWorldOptions::Option &option) {
        return chosen().value(option.definition->name) != option.json(option.defaultValue);
    }));
}

void QcWorldOptionsController::reset()
{
    load();
    setChosen(QcWorldOptions::defaults(*content()));
}

void QcWorldOptionsController::setValue(const QString &name, const QVariant &value)
{
    const auto option = std::ranges::find(options(), name, [](const QcWorldOptions::Option &each) { return each.definition->name; });
    if (option == options().end()) {
        qCWarning(lcGame, "World option %s refused: unknown", qPrintable(name));
        return;
    }

    // A slider's value lies only near its step (0.6000000000000001): snapped to the option's grid,
    // the default compares equal again and world.json stores 0.6.
    const QJsonValue json = option->isSwitch() ? QJsonValue::fromVariant(value) : QJsonValue(option->snapped(value.toDouble()));
    const bool fits = (!option->isSwitch() || value.typeId() == QMetaType::Bool) && option->read(json, true).has_value();
    if (!fits) {
        qCWarning(lcGame, "World option %s refused: %s is outside %g..%g or of the wrong kind", qPrintable(name), qPrintable(value.toString()),
                  option->definition->min, option->definition->max);
        return;
    }

    QJsonObject values = chosen();
    values.insert(name, json);
    setChosen(values);
}

void QcWorldOptionsController::choosePreset(const QString &name)
{
    load();
    if (name.isEmpty()) {
        setChosen(QcWorldOptions::defaults(*content()));
        return;
    }

    const QcResult<QJsonObject> values = QcWorldOptions::presetValues(name, *content());
    if (!values) {
        qCWarning(lcGame, "World preset refused: %s", qPrintable(values.error()));
        return;
    }
    setChosen(*values);
}

void QcWorldOptionsController::retranslate()
{
    if (!content()) {
        return;
    }

    m_groups.clear();
    for (const QcWorldOptionGroup &group : content()->worldgen().worldOptionGroups()) {
        QVariantList options;
        for (const QcWorldOptions::Option &option : this->options()) {
            if (option.definition->group != group.name) {
                continue;
            }
            options.append(QVariantMap{{u"name"_s, option.definition->name},
                                       {u"label"_s, text(option.definition->translationKey)},
                                       {u"help"_s, text(option.definition->translationKey + u".help"_s)},
                                       {u"isSwitch"_s, option.isSwitch()},
                                       {u"from"_s, option.definition->min},
                                       {u"to"_s, option.definition->max},
                                       {u"stepSize"_s, option.definition->step},
                                       {u"unit"_s, QVariant::fromValue(unitOf(option.definition->unit))}});
        }
        if (!options.isEmpty()) {
            m_groups.append(QVariantMap{{u"name"_s, group.name}, {u"title"_s, text(group.translationKey)}, {u"options"_s, options}});
        }
    }

    m_presets.clear();
    for (const QcWorldPreset &preset : content()->worldgen().worldPresets()) {
        m_presets.append(QVariantMap{{u"name"_s, preset.name}, {u"title"_s, text(preset.translationKey)}, {u"help"_s, text(preset.translationKey + u".help"_s)}});
    }
    emit optionsChanged();
}

void QcWorldOptionsController::release()
{
    setContent(nullptr);
}

void QcWorldOptionsController::load()
{
    if (content() && m_contentGeneration == m_library.contentGeneration()) {
        return;
    }

    m_library.content();
    setContent(m_library.loadedContent());
    m_contentGeneration = m_library.contentGeneration();
    retranslate();
}

void QcWorldOptionsController::setContent(std::shared_ptr<const QcContentRegistry> content)
{
    m_options = content ? QcWorldOptions::optionsOf(*content) : QList<QcWorldOptions::Option>{};
    m_content = std::move(content);
}

void QcWorldOptionsController::setChosen(const QJsonObject &values)
{
    // The same values (a preset chosen again) change nothing.
    if (values == m_values) {
        return;
    }
    m_values = values;
    emit valuesChanged();
}

// The content's text of `key` in the current language; empty when no language has it (a help line
// a mod left out).
QString QcWorldOptionsController::text(const QString &key) const
{
    const QString language = m_language();
    const QcTranslations &translations = content()->translations();
    return translations.has(key, language) || translations.has(key, u"en"_s) ? translations.translate(key, language) : QString();
}
