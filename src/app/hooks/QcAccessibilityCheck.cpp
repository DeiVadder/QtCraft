#include "app/hooks/QcAccessibilityCheck.h"

#include <QQuickItem>
#include <QRegularExpression>
#include <QtQml/qqml.h>
#include <QtQuick/private/qquickaccessibleattached_p.h>

#include <array>

using namespace Qt::StringLiterals;

namespace {

// Controls whose text is a value (or that have none): they need Accessible.name.
constexpr std::array explicitNameTypes{"QQuickSlider", "QQuickRangeSlider", "QQuickSpinBox", "QQuickComboBox", "QQuickDial",
                                       "QQuickTextInput", "QQuickTextEdit", "QQuickListView", "QQuickGridView"};
// Controls made of other items (an editable box's text field, a spin box's field): checked as one.
constexpr std::array compoundTypes{"QQuickSpinBox", "QQuickComboBox"};
// Roles of an item that takes clicks.
constexpr std::array clickableRoles{QAccessible::Button, QAccessible::CheckBox, QAccessible::RadioButton, QAccessible::ListItem,
                                    QAccessible::PageTab, QAccessible::Link, QAccessible::MenuItem, QAccessible::Cell,
                                    QAccessible::ComboBox, QAccessible::Slider, QAccessible::SpinBox, QAccessible::ButtonMenu};

QQuickAccessibleAttached *attachedOf(const QQuickItem &item)
{
    // Read only: an item without Accessible properties gets none here.
    return qobject_cast<QQuickAccessibleAttached *>(
        qmlAttachedPropertiesObject<QQuickAccessibleAttached>(const_cast<QQuickItem *>(&item), false));
}

QString describe(const QQuickItem &item, const QString &missing)
{
    const QRectF rect = item.mapRectToScene(QRectF(0.0, 0.0, item.width(), item.height()));
    return u"%1 \"%2\" (%3) at %4,%5 size %6x%7"_s.arg(QString::fromLatin1(item.metaObject()->className()), item.objectName(), missing)
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
}

} // namespace

QStringList QcAccessibilityCheck::unnamedControls(const QQuickItem &root)
{
    QStringList found;
    const QList<QQuickItem *> children = root.childItems();
    for (const QQuickItem *child : children) {
        found += check(*child);
    }
    return found;
}

QString QcAccessibilityCheck::accessibleName(const QQuickItem &item)
{
    if (const QQuickAccessibleAttached *attached = attachedOf(item); attached && !attached->name().isEmpty()) {
        return attached->name();
    }
    if (inherits(item, "QQuickAbstractButton")) {
        return item.property("text").toString();
    }
    return {};
}

QStringList QcAccessibilityCheck::check(const QQuickItem &item)
{
    QStringList found;
    if (!item.isVisible() || item.opacity() <= 0.0 || ignored(item)) {
        return found;
    }

    if (const QString lacks = missing(item); !lacks.isEmpty()) {
        found.append(describe(item, lacks));
    }

    for (const char *type : compoundTypes) {
        if (inherits(item, type)) {
            return found;
        }
    }
    const QList<QQuickItem *> children = item.childItems();
    for (const QQuickItem *child : children) {
        found += check(*child);
    }
    return found;
}

QString QcAccessibilityCheck::missing(const QQuickItem &item)
{
    for (const char *type : explicitNameTypes) {
        if (!inherits(item, type)) {
            continue;
        }

        // The list inside a tab bar (or another container) is the container's own.
        if (item.parentItem() && item.parentItem()->inherits("QQuickContainer")) {
            return {};
        }
        // Text that cannot be edited is read, not used.
        if ((inherits(item, "QQuickTextInput") || inherits(item, "QQuickTextEdit")) && item.property("readOnly").toBool()) {
            return {};
        }
        const QQuickAccessibleAttached *attached = attachedOf(item);
        return attached && meaningful(attached->name()) ? QString() : u"no Accessible.name"_s;
    }

    if (inherits(item, "QQuickAbstractButton")) {
        return meaningful(accessibleName(item)) ? QString() : u"no name: text \"%1\""_s.arg(item.property("text").toString());
    }
    if ((inherits(item, "QQuickMouseArea") && item.isEnabled()) || hasTapHandler(item)) {
        return missingOnClickable(item);
    }
    return {};
}

QString QcAccessibilityCheck::missingOnClickable(const QQuickItem &item)
{
    // The item itself or the one it fills (a MouseArea inside its button-like Rectangle).
    QString lacks = u"no Accessible.name"_s;
    for (const QQuickItem *candidate : {&item, static_cast<const QQuickItem *>(item.parentItem())}) {
        if (!candidate || !meaningful(accessibleName(*candidate))) {
            continue;
        }
        if (clickableRole(*candidate)) {
            return {};
        }
        lacks = u"no clickable Accessible.role"_s;
    }
    return lacks;
}

bool QcAccessibilityCheck::inherits(const QObject &object, const char *className)
{
    return object.inherits(className);
}

bool QcAccessibilityCheck::hasTapHandler(const QQuickItem &item)
{
    const QObjectList &children = item.children();
    for (const QObject *child : children) {
        if (inherits(*child, "QQuickTapHandler") && child->property("enabled").toBool()) {
            return true;
        }
    }
    return false;
}

bool QcAccessibilityCheck::ignored(const QQuickItem &item)
{
    const QQuickAccessibleAttached *attached = attachedOf(item);
    return attached && attached->ignored();
}

bool QcAccessibilityCheck::clickableRole(const QQuickItem &item)
{
    const QQuickAccessibleAttached *attached = attachedOf(item);
    if (!attached) {
        return false;
    }
    for (const QAccessible::Role role : clickableRoles) {
        if (attached->role() == role) {
            return true;
        }
    }
    return false;
}

bool QcAccessibilityCheck::meaningful(const QString &name)
{
    static const QRegularExpression letterOrDigit(u"[\\p{L}\\p{N}]"_s);
    return letterOrDigit.match(name).hasMatch();
}
