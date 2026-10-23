#include "common/gameplay/QcKeyBindings.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

struct ActionRow
{
    QcKeyBindings::Action action = QcKeyBindings::Action::Forward;
    QStringView id;
    int primary = 0;
    int secondary = 0;
};

using A = QcKeyBindings::Action;

// The default table. Primary keys are never function keys except F11 (see the class comment). Sprint is
// replaced on a Mac (macSprint), where Qt::Key_Control is Cmd.
constexpr std::array<ActionRow, QcKeyBindings::actionCount> actionRows{{
    {A::Forward, u"forward", Qt::Key_W, 0},
    {A::Back, u"back", Qt::Key_S, 0},
    {A::Left, u"left", Qt::Key_A, 0},
    {A::Right, u"right", Qt::Key_D, 0},
    {A::Jump, u"jump", Qt::Key_Space, 0},
    {A::Sneak, u"sneak", Qt::Key_Shift, 0},
    {A::Sprint, u"sprint", Qt::Key_Control, 0},
    {A::LookUp, u"lookUp", Qt::Key_Up, 0},
    {A::LookDown, u"lookDown", Qt::Key_Down, 0},
    {A::LookLeft, u"lookLeft", Qt::Key_Left, 0},
    {A::LookRight, u"lookRight", Qt::Key_Right, 0},
    {A::Mine, u"mine", Qt::Key_F, 0},
    {A::Use, u"use", Qt::Key_R, 0},
    {A::PickBlock, u"pickBlock", Qt::Key_B, 0},
    {A::Drop, u"drop", Qt::Key_Q, 0},
    {A::Inventory, u"inventory", Qt::Key_E, 0},
    {A::Chat, u"chat", Qt::Key_T, Qt::Key_Return},
    {A::Command, u"command", Qt::Key_Slash, 0},
    {A::CameraView, u"cameraView", Qt::Key_V, Qt::Key_F5},
    {A::Handbook, u"handbook", Qt::Key_H, Qt::Key_F1},
    {A::DebugOverlay, u"debugOverlay", Qt::Key_I, Qt::Key_F3},
    {A::DebugOptions, u"debugOptions", Qt::Key_O, Qt::Key_F4},
    {A::Labels, u"labels", Qt::Key_L, 0},
    {A::PlayerList, u"playerList", Qt::Key_Tab, 0},
    {A::Hotbar1, u"hotbar1", Qt::Key_1, 0},
    {A::Hotbar2, u"hotbar2", Qt::Key_2, 0},
    {A::Hotbar3, u"hotbar3", Qt::Key_3, 0},
    {A::Hotbar4, u"hotbar4", Qt::Key_4, 0},
    {A::Hotbar5, u"hotbar5", Qt::Key_5, 0},
    {A::Hotbar6, u"hotbar6", Qt::Key_6, 0},
    {A::Hotbar7, u"hotbar7", Qt::Key_7, 0},
    {A::Hotbar8, u"hotbar8", Qt::Key_8, 0},
    {A::Hotbar9, u"hotbar9", Qt::Key_9, 0},
    {A::HotbarRadial, u"hotbarRadial", Qt::Key_G, 0},
    // P for photo; F6 and F2 as the familiar function keys (F1 is the handbook's, decision E-692).
    {A::HideHud, u"hideHud", Qt::Key_P, Qt::Key_F6},
    {A::Screenshot, u"screenshot", Qt::Key_K, Qt::Key_F2},
    // F11 as in other games and browsers (decision E-835); a Mac gets it as secondary key (macFullscreen).
    {A::Fullscreen, u"fullscreen", Qt::Key_F11, 0},
    {A::Pause, u"pause", Qt::Key_Escape, 0},
}};

struct NamedKey
{
    int key = 0;
    QStringView name;
};

// Keys without a printable character of their own. Printable keys are named by their character.
constexpr std::array namedKeys{
    NamedKey{Qt::Key_Space, u"Space"},
    NamedKey{Qt::Key_Escape, u"Esc"},
    NamedKey{Qt::Key_Tab, u"Tab"},
    NamedKey{Qt::Key_Backspace, u"Backspace"},
    NamedKey{Qt::Key_Return, u"Return"},
    NamedKey{Qt::Key_Enter, u"Enter"},
    NamedKey{Qt::Key_Insert, u"Insert"},
    NamedKey{Qt::Key_Delete, u"Delete"},
    NamedKey{Qt::Key_Home, u"Home"},
    NamedKey{Qt::Key_End, u"End"},
    NamedKey{Qt::Key_PageUp, u"PageUp"},
    NamedKey{Qt::Key_PageDown, u"PageDown"},
    NamedKey{Qt::Key_Left, u"Left"},
    NamedKey{Qt::Key_Up, u"Up"},
    NamedKey{Qt::Key_Right, u"Right"},
    NamedKey{Qt::Key_Down, u"Down"},
    NamedKey{Qt::Key_Shift, u"Shift"},
    NamedKey{Qt::Key_Control, u"Control"},
    NamedKey{Qt::Key_Meta, u"Meta"},
    NamedKey{Qt::Key_Alt, u"Alt"},
    NamedKey{Qt::Key_AltGr, u"AltGr"},
    NamedKey{Qt::Key_CapsLock, u"CapsLock"},
};

// Option: macOS uses no Option shortcuts while a game has the focus (it only types characters).
constexpr int macSprint = Qt::Key_Alt;

// Qt::Key values below this are Unicode characters (upper case for letters).
constexpr int firstSpecialKey = 0x01000000;

QString macName(int key)
{
    switch (key) {
    case Qt::Key_Control:
        return u"MacCmd"_s;
    case Qt::Key_Meta:
        return u"MacControl"_s;
    case Qt::Key_Alt:
        return u"MacOption"_s;
    default:
        return QcKeyBindings::keyName(key);
    }
}

} // namespace

QcKeyBindings::Platform QcKeyBindings::currentPlatform()
{
#if defined(Q_OS_MACOS)
    return Platform::Mac;
#else
    return Platform::Other;
#endif
}

QcKeyBindings QcKeyBindings::defaults(Platform platform)
{
    QcKeyBindings bindings;
    bindings.m_platform = platform;

    for (const ActionRow &row : actionRows) {
        bindings.set(row.action, 0, row.primary);
        bindings.set(row.action, 1, row.secondary);
    }

    if (platform == Platform::Mac) {
        bindings.set(A::Sprint, 0, macSprint);
        // F11 needs fn on a Mac: a secondary key, like the other function keys.
        bindings.set(A::Fullscreen, 0, 0);
        bindings.set(A::Fullscreen, 1, Qt::Key_F11);
    }
    return bindings;
}

Qt::KeyboardModifier QcKeyBindings::stackModifier(Platform platform)
{
    return platform == Platform::Mac ? Qt::MetaModifier : Qt::ControlModifier;
}

const QList<QcKeyBindings::Action> &QcKeyBindings::actions()
{
    static const QList<Action> list = [] {
        QList<Action> all;
        all.reserve(actionCount);
        for (const ActionRow &row : actionRows) {
            all.append(row.action);
        }
        return all;
    }();
    return list;
}

QString QcKeyBindings::id(Action action)
{
    return actionRows[static_cast<std::size_t>(action)].id.toString();
}

std::optional<QcKeyBindings::Action> QcKeyBindings::actionFromId(const QString &id)
{
    for (const ActionRow &row : actionRows) {
        if (row.id == id) {
            return row.action;
        }
    }
    return std::nullopt;
}

bool QcKeyBindings::isBindable(int key, Platform platform)
{
    if (platform == Platform::Mac && key == Qt::Key_Control) {
        return false;
    }
    return key > 0 && key != Qt::Key_Escape && key != clearKey && key != Qt::Key_unknown && !keyName(key).isEmpty();
}

bool QcKeyBindings::isFunctionKey(int key)
{
    return key >= Qt::Key_F1 && key <= Qt::Key_F35;
}

QString QcKeyBindings::keyName(int key)
{
    if (key <= 0) {
        return {};
    }

    for (const NamedKey &named : namedKeys) {
        if (named.key == key) {
            return named.name.toString();
        }
    }

    if (isFunctionKey(key)) {
        return u"F%1"_s.arg(key - Qt::Key_F1 + 1);
    }

    if (key < firstSpecialKey) {
        const char32_t code = static_cast<char32_t>(key);
        const QString text = QString::fromUcs4(&code, 1);
        // Printable characters only; Qt reports letters in upper case (lower-case letters are no keys).
        const QString upper = text.toUpper();
        if (!text.isEmpty() && text.at(0).isPrint() && !text.at(0).isSpace() && (upper == text || upper.size() != text.size())) {
            return text;
        }
    }
    return {};
}

std::optional<int> QcKeyBindings::keyFromName(const QString &name)
{
    if (name.isEmpty()) {
        return 0;
    }

    for (const NamedKey &named : namedKeys) {
        if (named.name == name) {
            return named.key;
        }
    }

    if (name.size() >= 2 && name.size() <= 3 && name.startsWith(u'F')) {
        bool ok = false;
        const int number = QStringView(name).mid(1).toInt(&ok);
        if (ok && number >= 1 && number <= 35) {
            return Qt::Key_F1 + number - 1;
        }
        return std::nullopt;
    }

    const QString upper = name.toUpper();
    const QList<uint> codes = (upper.size() == name.size() ? upper : name).toUcs4();
    if (codes.size() != 1) {
        return std::nullopt;
    }

    const int key = static_cast<int>(codes.first());
    if (key >= firstSpecialKey || keyName(key).isEmpty()) {
        return std::nullopt;
    }
    return key;
}

int QcKeyBindings::key(Action action, int slot) const
{
    if (slot < 0 || slot >= slotCount) {
        return 0;
    }
    return m_keys[static_cast<std::size_t>(action)][static_cast<std::size_t>(slot)];
}

QList<int> QcKeyBindings::keys(Action action) const
{
    QList<int> list;
    for (const int key : m_keys[static_cast<std::size_t>(action)]) {
        if (key != 0) {
            list.append(key);
        }
    }
    return list;
}

bool QcKeyBindings::matches(Action action, int key) const
{
    return key != 0 && std::ranges::find(m_keys[static_cast<std::size_t>(action)], key) != m_keys[static_cast<std::size_t>(action)].end();
}

std::optional<QcKeyBindings::Action> QcKeyBindings::actionFor(int key) const
{
    for (const ActionRow &row : actionRows) {
        if (matches(row.action, key)) {
            return row.action;
        }
    }
    return std::nullopt;
}

std::optional<QcKeyBindings::Action> QcKeyBindings::actionForPress(int key, Qt::KeyboardModifiers modifiers) const
{
    if (m_platform == Platform::Mac && modifiers.testFlag(Qt::ControlModifier)) {
        return std::nullopt;
    }
    return actionFor(key);
}

std::optional<QcKeyBindings::Displaced> QcKeyBindings::bind(Action action, int slot, int key)
{
    if (isFixed(action) || slot < 0 || slot >= slotCount || (key != 0 && !isBindable(key, m_platform))) {
        return std::nullopt;
    }

    const int previous = this->key(action, slot);
    if (previous == key) {
        return std::nullopt;
    }

    std::optional<Displaced> displaced;
    // Unbinding (key 0) displaces nothing.
    for (const ActionRow &row : actionRows) {
        for (int other = 0; other < slotCount; ++other) {
            if (key == 0 || (row.action == action && other == slot) || this->key(row.action, other) != key) {
                continue;
            }
            // The other slot takes this slot's old key; another action that has it already gets none.
            const int replacement = row.action != action && matches(row.action, previous) ? 0 : previous;
            set(row.action, other, replacement);
            displaced = Displaced{row.action, other, replacement};
        }
    }

    set(action, slot, key);
    return displaced;
}

void QcKeyBindings::setKey(Action action, int slot, int key)
{
    if (!isFixed(action) && slot >= 0 && slot < slotCount && (key == 0 || isBindable(key, m_platform))) {
        set(action, slot, key);
    }
}

QList<std::pair<QcKeyBindings::Action, int>> QcKeyBindings::conflicts() const
{
    QList<std::pair<Action, int>> list;
    QSet<int> seen;
    for (const ActionRow &row : actionRows) {
        for (int slot = 0; slot < slotCount; ++slot) {
            const int key = this->key(row.action, slot);
            if (key == 0) {
                continue;
            }

            if (seen.contains(key)) {
                list.append({row.action, slot});
            }
            seen.insert(key);
        }
    }
    return list;
}

QList<std::pair<QcKeyBindings::Action, int>> QcKeyBindings::dropConflicts(const QList<std::pair<Action, int>> &preferred)
{
    // Who keeps each key: preferred slots first, then the table order.
    QHash<int, std::pair<Action, int>> owners;
    const auto claim = [this, &owners](Action action, int slot) {
        if (const int key = this->key(action, slot); key != 0 && !owners.contains(key)) {
            owners.insert(key, {action, slot});
        }
    };
    for (const auto &[action, slot] : preferred) {
        claim(action, slot);
    }
    for (const ActionRow &row : actionRows) {
        for (int slot = 0; slot < slotCount; ++slot) {
            claim(row.action, slot);
        }
    }

    QList<std::pair<Action, int>> dropped;
    for (const ActionRow &row : actionRows) {
        for (int slot = 0; slot < slotCount; ++slot) {
            const int key = this->key(row.action, slot);
            if (key != 0 && owners.value(key) != std::pair(row.action, slot)) {
                set(row.action, slot, 0);
                dropped.append({row.action, slot});
            }
        }
    }
    return dropped;
}

QMap<QString, QStringList> QcKeyBindings::placeholderKeys() const
{
    QMap<QString, QStringList> values;
    for (const ActionRow &row : actionRows) {
        QStringList names;
        const QList<int> bound = keys(row.action);
        for (const int key : bound) {
            names.append(m_platform == Platform::Mac ? macName(key) : keyName(key));
        }
        if (m_platform == Platform::Mac && !bound.isEmpty() && isFunctionKey(bound.first())) {
            names.first().prepend(u"fn+"_s);
        }

        values.insert(u"key."_s + row.id, names);
        values.insert(u"key1."_s + row.id, names.mid(0, 1));
    }

    // The modifier of the drop key for the whole stack.
    values.insert(u"key.stackModifier"_s, QStringList{m_platform == Platform::Mac ? macName(Qt::Key_Meta) : keyName(Qt::Key_Control)});
    return values;
}
