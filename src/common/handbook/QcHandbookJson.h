// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbook.h"

#include <QJsonArray>
#include <QJsonObject>

/*! \brief JSON form of handbook pages, shared by the control interface and the in-game screen
    (which turns it into QVariantMaps), so both show exactly the same content. */
namespace QcHandbookJson {

/*! Returns the page of \a entry with the category and pack names of \a handbook: {"id", "name",
    "category", "categoryTitle", "pack", "packName", "title", "icon", "text", "facts": [{"label",
    "value", "links": [link]}], "sections": [{"title", "text", "links": [link], "recipes":
    [recipe]}]} with link = {"target", "text", "icon", "note"} and recipe = {"kind"
    ("crafting"/"machine"), "name", "shapeless", "needsTable", "width", "height", "cells": [link
    or null], "result": link, "count", "seconds", "machines": [link], "note"}. */
QJsonObject entry(const QcHandbook &handbook, const QcHandbookEntry &entry);
/*! Returns {"id", "title", "icon", "category", "pack"} of \a entry for lists and search results. */
QJsonObject summary(const QcHandbookEntry &entry);
/*! Returns [{"id", "title", "count"}] of the non-empty categories of \a handbook. */
QJsonArray categories(const QcHandbook &handbook);

} // namespace QcHandbookJson
