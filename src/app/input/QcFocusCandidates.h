// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QRectF>

class QQuickItem;
class QQuickWindow;

// Which items of a window the pad's focus can reach (decision E-583), from the item tree alone, so
// every screen takes part without a list of its own: controls that take focus by Tab (focusPolicy
// or activeFocusOnTab, e.g. inventory slots), rows of lists (item delegates), editable text fields, all of them shown, enabled and
// not transparent, within the scope: the topmost open popup that has candidates, else the topmost
// shown screen (child of the window's content item) that has them.
namespace QcFocusCandidates {

// The popup or screen the focus moves within; nullptr when nothing shown has a candidate. Offscreen
// runs move the screens into the content item of another window (QcOffscreenRender): the window's
// content item holds them either way.
QQuickItem *scope(QQuickWindow *window);
// The candidates of `scope` in tree order.
QList<QQuickItem *> candidates(const QQuickItem *scope);
// Whether `item` itself can take the pad's focus (shown, enabled, focusable, not empty).
bool isCandidate(QQuickItem *item);
// An editable text field or area: confirming it opens the on-screen keyboard.
bool isTextInput(QQuickItem *item);
// Whether `item` is `ancestor` or lies inside it.
bool contains(const QQuickItem *ancestor, const QQuickItem *item);
// Whether `item` shows in every list or page around it (not scrolled out of view).
bool inView(const QQuickItem *item);
// The list or page (Flickable) nearest around `item`, nullptr for none.
QQuickItem *flickableAround(const QQuickItem *item);
// The item's bounds in the window.
QRectF sceneRect(const QQuickItem *item);
// The part of the item's bounds that the lists and pages around it show (empty: scrolled away).
QRectF visibleRect(const QQuickItem *item);
// Whether the lists and pages around `item` show all of it.
bool wholeInView(const QQuickItem *item);

} // namespace QcFocusCandidates
