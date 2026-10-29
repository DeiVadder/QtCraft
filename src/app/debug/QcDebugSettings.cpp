// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/debug/QcDebugSettings.h"

#include <algorithm>

QcDebugSettings::QcDebugSettings(QObject *parent)
    : QObject(parent)
{
}

void QcDebugSettings::load(QcDebugOptions options)
{
    const bool levelChanges = options.level != m_options.level;
    const bool visualsChange = options.visuals != m_options.visuals;
    m_options = options;
    if (levelChanges) {
        emit levelChanged();
    }
    if (visualsChange) {
        emit visualsChanged();
    }
}

void QcDebugSettings::setLevel(int level)
{
    const auto value = static_cast<QcDebugOptions::Level>(std::clamp(level, 0, static_cast<int>(QcDebugOptions::Level::Full)));
    if (value != m_options.level) {
        m_options.level = value;
        emit levelChanged();
    }
}

void QcDebugSettings::cycleLevel()
{
    setLevel(static_cast<int>(QcDebugOptions::nextLevel(m_options.level)));
}

void QcDebugSettings::setShown(Visual visual, bool on)
{
    if (m_options.shows(visual) != on) {
        m_options.set(visual, on);
        emit visualsChanged();
    }
}
