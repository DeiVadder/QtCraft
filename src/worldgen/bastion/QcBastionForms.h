// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/bastion/QcBastionSketch.h"

/*! \brief The four forms of a bastion ruin (decision E-1087), each an own design in cells around
    its middle: a causeway over a canal of lava, stables, a treasure hall and a walled quarter. */
namespace QcBastionForms {

/*! Draws \a form on \a sketch. */
void draw(QcBastionSketch &sketch, QcBastionPlan::Form form);

} // namespace QcBastionForms
