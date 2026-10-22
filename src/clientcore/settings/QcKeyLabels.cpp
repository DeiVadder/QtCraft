#include "clientcore/settings/QcKeyLabels.h"

using namespace Qt::StringLiterals;

QString QcKeyLabels::key(int key)
{
    switch (key) {
    case 0:
        return u"–"_s;
    case Qt::Key_Space:
        return tr("Space");
    case Qt::Key_Shift:
        return tr("Shift");
#if defined(Q_OS_MACOS)
    case Qt::Key_Control:
        return u"Cmd"_s;
    case Qt::Key_Meta:
        return u"Control"_s;
    case Qt::Key_Alt:
        return u"Option"_s;
#else
    case Qt::Key_Control:
        return tr("Ctrl");
    case Qt::Key_Meta:
        return u"Meta"_s;
    case Qt::Key_Alt:
        return u"Alt"_s;
#endif
    case Qt::Key_Return:
        return tr("Enter");
    case Qt::Key_Enter:
        return tr("Keypad Enter");
    case Qt::Key_Backspace:
        return tr("Backspace");
    case Qt::Key_Delete:
        return tr("Del");
    case Qt::Key_Insert:
        return tr("Insert");
    case Qt::Key_Home:
        return tr("Home");
    case Qt::Key_End:
        return tr("End");
    case Qt::Key_PageUp:
        return tr("Page Up");
    case Qt::Key_PageDown:
        return tr("Page Down");
    case Qt::Key_CapsLock:
        return tr("Caps Lock");
    case Qt::Key_Up:
        return u"↑"_s;
    case Qt::Key_Down:
        return u"↓"_s;
    case Qt::Key_Left:
        return u"←"_s;
    case Qt::Key_Right:
        return u"→"_s;
    default:
        break;
    }

    const QString name = QcKeyBindings::keyName(key);
    return name.isEmpty() ? u"?"_s : name;
}

// Walking, jumping and looking around.
QString QcKeyLabels::movementAction(QcKeyBindings::Action action)
{
    using A = QcKeyBindings::Action;
    switch (action) {
    case A::Forward:
        return tr("Walk forward");
    case A::Back:
        return tr("Walk back");
    case A::Left:
        return tr("Walk left");
    case A::Right:
        return tr("Walk right");
    case A::Jump:
        return tr("Jump, swim up");
    case A::Sneak:
        return tr("Sneak");
    case A::Sprint:
        return tr("Sprint");
    case A::LookUp:
        return tr("Look up");
    case A::LookDown:
        return tr("Look down");
    case A::LookLeft:
        return tr("Turn left");
    case A::LookRight:
        return tr("Turn right");
    // The actions of the other groups.
    case A::Mine:
    case A::Use:
    case A::PickBlock:
    case A::Drop:
    case A::Inventory:
    case A::Chat:
    case A::Command:
    case A::CameraView:
    case A::Handbook:
    case A::DebugOverlay:
    case A::DebugOptions:
    case A::Labels:
    case A::PlayerList:
    case A::Hotbar1:
    case A::Hotbar2:
    case A::Hotbar3:
    case A::Hotbar4:
    case A::Hotbar5:
    case A::Hotbar6:
    case A::Hotbar7:
    case A::Hotbar8:
    case A::Hotbar9:
    case A::HotbarRadial:
    case A::HideHud:
    case A::Screenshot:
    case A::Fullscreen:
    case A::Pause:
        return {};
    }
    // Every action is a case above; MSVC still wants a return after the switch (C4715).
    return {};
}

// What the hands do: mining, using, picking, throwing and the hotbar slots.
QString QcKeyLabels::handAction(QcKeyBindings::Action action)
{
    using A = QcKeyBindings::Action;
    switch (action) {
    case A::Mine:
        return tr("Mine, attack");
    case A::Use:
        return tr("Place, use, eat");
    case A::PickBlock:
        return tr("Pick block (also middle mouse button)");
    case A::Drop:
        return tr("Throw item (with %1: stack)")
            .arg(key(QcKeyBindings::stackModifier(QcKeyBindings::currentPlatform()) == Qt::MetaModifier ? Qt::Key_Meta : Qt::Key_Control));
    case A::Hotbar1:
    case A::Hotbar2:
    case A::Hotbar3:
    case A::Hotbar4:
    case A::Hotbar5:
    case A::Hotbar6:
    case A::Hotbar7:
    case A::Hotbar8:
    case A::Hotbar9:
        return tr("Hotbar slot %1").arg(int(action) - int(A::Hotbar1) + 1);

    // The actions of the other groups.
    case A::Forward:
    case A::Back:
    case A::Left:
    case A::Right:
    case A::Jump:
    case A::Sneak:
    case A::Sprint:
    case A::LookUp:
    case A::LookDown:
    case A::LookLeft:
    case A::LookRight:
    case A::Inventory:
    case A::Chat:
    case A::Command:
    case A::CameraView:
    case A::Handbook:
    case A::DebugOverlay:
    case A::DebugOptions:
    case A::Labels:
    case A::PlayerList:
    case A::HotbarRadial:
    case A::HideHud:
    case A::Screenshot:
    case A::Fullscreen:
    case A::Pause:
        return {};
    }
    // Every action is a case above; MSVC still wants a return after the switch (C4715).
    return {};
}

// Screens, the camera, the debug display and the other parts of the interface.
QString QcKeyLabels::screenAction(QcKeyBindings::Action action)
{
    using A = QcKeyBindings::Action;
    switch (action) {
    case A::Inventory:
        return tr("Inventory");
    case A::Chat:
        return tr("Chat");
    case A::Command:
        return tr("Command");
    case A::CameraView:
        return tr("Camera view");
    case A::Handbook:
        return tr("Handbook");
    case A::DebugOverlay:
        return tr("Debug display (off, compact, full)");
    case A::DebugOptions:
        return tr("Debug options");
    case A::Labels:
        return tr("Labels in the world");
    case A::PlayerList:
        return tr("Player list (hold)");
    case A::HotbarRadial:
        return tr("Radial menu of the hotbar (hold, choose with the mouse)");
    case A::HideHud:
        return tr("Hide HUD (photo mode)");
    case A::Screenshot:
        return tr("Screenshot");
    case A::Fullscreen:
        return tr("Fullscreen on and off");
    case A::Pause:
        return tr("Menu");
    // The actions of the other groups.
    case A::Forward:
    case A::Back:
    case A::Left:
    case A::Right:
    case A::Jump:
    case A::Sneak:
    case A::Sprint:
    case A::LookUp:
    case A::LookDown:
    case A::LookLeft:
    case A::LookRight:
    case A::Mine:
    case A::Use:
    case A::PickBlock:
    case A::Drop:
    case A::Hotbar1:
    case A::Hotbar2:
    case A::Hotbar3:
    case A::Hotbar4:
    case A::Hotbar5:
    case A::Hotbar6:
    case A::Hotbar7:
    case A::Hotbar8:
    case A::Hotbar9:
        return {};
    }
    // Every action is a case above; MSVC still wants a return after the switch (C4715).
    return {};
}

QString QcKeyLabels::action(QcKeyBindings::Action action)
{
    if (QString label = movementAction(action); !label.isEmpty()) {
        return label;
    }
    if (QString label = handAction(action); !label.isEmpty()) {
        return label;
    }
    return screenAction(action);
}
