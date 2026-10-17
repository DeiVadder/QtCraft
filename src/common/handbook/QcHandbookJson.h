#pragma once

#include "common/handbook/QcHandbook.h"

#include <QJsonArray>
#include <QJsonObject>

// JSON form of handbook pages, shared by the control interface and the in-game screen (which turns it
// into QVariantMaps), so both show exactly the same content.
namespace QcHandbookJson {

// {"id", "name", "category", "categoryTitle", "pack", "packName", "title", "icon", "text",
//  "facts": [{"label", "value", "links": [link]}],
//  "sections": [{"title", "text", "links": [link], "recipes": [recipe]}]}
// with link = {"target", "text", "icon", "note"} and recipe = {"kind" ("crafting"/"machine"), "name",
// "shapeless", "needsTable", "width", "height", "cells": [link or null], "result": link, "count",
// "seconds", "machines": [link], "note"}.
QJsonObject entry(const QcHandbook &handbook, const QcHandbookEntry &entry);
// {"id", "title", "icon", "category", "pack"} for lists and search results.
QJsonObject summary(const QcHandbookEntry &entry);
// [{"id", "title", "count"}] of the non-empty categories.
QJsonArray categories(const QcHandbook &handbook);

} // namespace QcHandbookJson
