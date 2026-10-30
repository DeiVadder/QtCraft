// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>

/*! \brief Which output device the game should play through when the system's devices change (a
    headset switched on, a speaker unplugged): the system's default output, as long as there is one.

    Pure, on device ids only, so that it can be checked without real devices. */
class QcAudioRoute
{
public:
    /*! \brief What to do with the output. */
    struct Decision
    {
        /*! The kinds of decision. */
        enum class Action {
            /*! Keep the device playing (or none). */
            Keep,
            /*! Switch to \l device. */
            Switch,
            /*! Close the device: none is left. */
            Close
        };

        /*! What to do. */
        Action action = Action::Keep;
        /*! The device to switch to (Switch only). */
        QByteArray device;

        friend bool operator==(const Decision &, const Decision &) = default;
    };

    /*! Returns what to do. \a current: the device playing now, empty when none is open;
        \a outputs: the devices there are; \a defaultOutput: the system's default output, empty
        when it has none. Switches to the default when it changed, when the current device went
        away, and when a device appeared while none was open; closes when the current device went
        away and none is left. */
    [[nodiscard]] static Decision decide(const QByteArray &current, const QList<QByteArray> &outputs, const QByteArray &defaultOutput);
};
