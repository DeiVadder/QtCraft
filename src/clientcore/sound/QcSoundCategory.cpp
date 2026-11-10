// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcSoundCategory.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QString QcSoundCategories::name(QcSoundCategory category)
{
    switch (category) {
    case QcSoundCategory::Music:
        return u"music"_s;
    case QcSoundCategory::Weather:
        return u"weather"_s;
    case QcSoundCategory::Blocks:
        return u"blocks"_s;
    case QcSoundCategory::Hostile:
        return u"hostile"_s;
    case QcSoundCategory::Friendly:
        return u"friendly"_s;
    case QcSoundCategory::Players:
        return u"players"_s;
    case QcSoundCategory::Ambient:
        return u"ambient"_s;
    case QcSoundCategory::Interface:
        return u"interface"_s;
    }
    return {};
}

std::optional<QcSoundCategory> QcSoundCategories::fromName(const QString &text)
{
    for (const QcSoundCategory category : all()) {
        if (text == name(category)) {
            return category;
        }
    }
    return std::nullopt;
}

std::optional<QcSoundCategory> QcSoundCategories::fromIndex(int index)
{
    if (index < 0 || index >= qcSoundCategoryCount) {
        return std::nullopt;
    }
    return all()[static_cast<std::size_t>(index)];
}

void QcSoundVolumes::setVolume(QcSoundCategory category, double volume)
{
    const auto index = static_cast<std::size_t>(category);
    if (index < m_volumes.size()) {
        m_volumes[index] = clamped(volume, m_volumes[index]);
    }
}

double QcSoundVolumes::clamped(double volume, double fallback)
{
    return std::isfinite(volume) ? std::clamp(volume, 0.0, 1.0) : fallback;
}
