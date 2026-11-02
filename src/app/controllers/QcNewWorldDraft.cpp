// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcNewWorldDraft.h"

#include "app/controllers/QcWorldOptionsController.h"
#include "app/debug/QcAppLogging.h"

#include <QMetaEnum>
#include <QRandomGenerator>

using namespace Qt::StringLiterals;

namespace {

// Whether `value` is one of the values of the enumeration `Enum`.
template<typename Enum>
[[nodiscard]] bool isValueOf(Enum value)
{
    return QMetaEnum::fromType<Enum>().valueToKey(static_cast<int>(value)) != nullptr;
}

} // namespace

QcNewWorldDraft::QcNewWorldDraft(QcWorldOptionsController &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_name(tr("New World"))
{
    // The map shows the world the form would create.
    connect(this, &QcNewWorldDraft::seedChanged, this, [this] { m_options.preview()->setSeedText(seed()); });
}

void QcNewWorldDraft::setName(const QString &name)
{
    if (name != m_name) {
        m_name = name;
        emit nameChanged();
    }
}

void QcNewWorldDraft::setWorldType(WorldType type)
{
    if (!isValueOf(type)) {
        qCWarning(lcGame) << "New world: refused the world type" << static_cast<int>(type);
        return;
    }
    if (type != m_worldType) {
        m_worldType = type;
        emit worldTypeChanged();
    }
}

void QcNewWorldDraft::setSeedText(const QString &text)
{
    if (text != m_seedText) {
        m_seedText = text;
        emit seedChanged();
    }
}

QString QcNewWorldDraft::seed() const
{
    const QString typed = seedText().trimmed();
    return typed.isEmpty() ? randomSeed() : typed;
}

void QcNewWorldDraft::setRandomSeed(const QString &seed)
{
    if (seed != m_randomSeed) {
        m_randomSeed = seed;
        emit seedChanged();
    }
}

void QcNewWorldDraft::setLayers(const QString &layers)
{
    if (layers != m_layers) {
        m_layers = layers;
        emit layersChanged();
    }
}

void QcNewWorldDraft::setGameMode(QcGameMode mode)
{
    if (!isValueOf(mode)) {
        qCWarning(lcGame) << "New world: refused the game mode" << static_cast<int>(mode);
        return;
    }
    if (mode != m_gameMode) {
        m_gameMode = mode;
        emit gameModeChanged();
    }
}

void QcNewWorldDraft::setKeepInventory(bool keep)
{
    if (keep != m_keepInventory) {
        m_keepInventory = keep;
        emit keepInventoryChanged();
    }
}

QVariantMap QcNewWorldDraft::setup() const
{
    return {{u"keepInventory"_s, keepInventory()},
            {u"worldType"_s, static_cast<int>(worldType())},
            {u"layers"_s, worldType() == WorldType::Flat ? layers() : QString()},
            {u"gameMode"_s, static_cast<int>(gameMode())},
            {u"worldOptions"_s, m_options.values()}};
}

void QcNewWorldDraft::setStartWorldType(WorldType type)
{
    if (!isValueOf(type)) {
        qCWarning(lcGame) << "New world: refused the start world type" << static_cast<int>(type);
        return;
    }
    m_startWorldType = type;
    setWorldType(type);
}

void QcNewWorldDraft::setStartGameMode(QcGameMode mode)
{
    if (!isValueOf(mode)) {
        qCWarning(lcGame) << "New world: refused the start game mode" << static_cast<int>(mode);
        return;
    }
    m_startGameMode = mode;
    setGameMode(mode);
}

void QcNewWorldDraft::reset()
{
    setName(tr("New World"));
    setWorldType(m_startWorldType);
    setSeedText({});
    setRandomSeed(drawSeed());
    setLayers({});
    setGameMode(m_startGameMode);
    setKeepInventory(true);
    m_options.reset();
    m_options.preview()->resetView();
}

void QcNewWorldDraft::rollSeed()
{
    setRandomSeed(drawSeed());
    setSeedText(randomSeed());
}

QString QcNewWorldDraft::drawSeed()
{
    return QString::number(QRandomGenerator::global()->bounded(maxRandomSeed));
}
