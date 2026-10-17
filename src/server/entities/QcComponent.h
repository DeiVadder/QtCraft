#pragma once

#include <QtGlobal>

#include <memory>
#include <utility>

// Optional state of one kind of creature, kept on the heap (decision E-423): only the creatures of
// that kind carry it (a villager its QcVillager), the others pay one pointer. Copies are deep, so a
// creature with components copies like a value (QList<QcCreature>). The saved form of a component
// is its frame in the creature's entry (QcComponentFrames, decision E-420).
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
    QcComponent &operator=(const QcComponent &other)
    {
        if (this != &other) {
            m_value = other.m_value ? std::make_unique<T>(*other.m_value) : nullptr;
        }
        return *this;
    }
    QcComponent &operator=(QcComponent &&) noexcept = default;
    ~QcComponent() = default;

    explicit operator bool() const { return m_value != nullptr; }
    bool has() const { return m_value != nullptr; }

    // The state; only while the creature has it.
    T &operator*()
    {
        Q_ASSERT(m_value);
        return *m_value;
    }
    const T &operator*() const
    {
        Q_ASSERT(m_value);
        return *m_value;
    }
    T *operator->() { return &**this; }
    const T *operator->() const { return &**this; }
    T *get() { return m_value.get(); }
    const T *get() const { return m_value.get(); }

    // Gives the creature the component (replacing one it had) and returns it.
    template<typename... Args>
    T &emplace(Args &&...args)
    {
        m_value = std::make_unique<T>(std::forward<Args>(args)...);
        return *m_value;
    }
    void reset() { m_value.reset(); }

private:
    std::unique_ptr<T> m_value;
};
