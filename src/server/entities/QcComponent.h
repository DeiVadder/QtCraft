// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <memory>
#include <utility>

/*! \brief Optional state \a T of one kind of creature, kept on the heap (decision E-423): only the
    creatures of that kind carry it (a villager its QcVillager), the others pay one pointer.

    Copies are deep, so a creature with components copies like a value (QList<QcCreature>). The
    saved form of a component is its frame in the creature's entry (QcComponentFrames, decision
    E-420). */
template<typename T>
class QcComponent
{
public:
    QcComponent() = default;
    QcComponent(const QcComponent &other)
        : m_value(other.m_value ? std::make_unique<T>(*other.m_value) : nullptr)
    {
    }
    QcComponent(QcComponent &&) noexcept = default;
    /*! Copies the state of \a other deeply (or none). Returns this component. */
    QcComponent &operator=(const QcComponent &other)
    {
        if (this != &other) {
            m_value = other.m_value ? std::make_unique<T>(*other.m_value) : nullptr;
        }
        return *this;
    }
    QcComponent &operator=(QcComponent &&) noexcept = default;
    ~QcComponent() = default;

    /*! Returns whether the creature has the component. */
    explicit operator bool() const { return m_value != nullptr; }
    /*! Returns whether the creature has the component. */
    bool has() const { return m_value != nullptr; }

    /*! Returns the state; only while the creature has it. */
    T &operator*()
    {
        Q_ASSERT(m_value);
        return *m_value;
    }
    /*! Returns the state, read-only; only while the creature has it. */
    const T &operator*() const
    {
        Q_ASSERT(m_value);
        return *m_value;
    }
    /*! Returns a pointer to the state; only while the creature has it. */
    T *operator->() { return &**this; }
    /*! Returns a pointer to the state, read-only; only while the creature has it. */
    const T *operator->() const { return &**this; }
    /*! Returns the state; nullptr when the creature has none. */
    T *get() { return m_value.get(); }
    /*! Returns the state, read-only; nullptr when the creature has none. */
    const T *get() const { return m_value.get(); }

    /*! Gives the creature the component made from \a args (replacing one it had). Returns it. */
    template<typename... Args>
    T &emplace(Args &&...args)
    {
        m_value = std::make_unique<T>(std::forward<Args>(args)...);
        return *m_value;
    }
    /*! Takes the component away. */
    void reset() { m_value.reset(); }

private:
    std::unique_ptr<T> m_value;
};
