#pragma once

#include <QtGlobal>

// Downcast of a polymorphic object whose type the caller already knows (a Qt event after its
// type() check, a scene-graph node or item the class built itself). Debug builds check the type
// with dynamic_cast; release builds cost nothing. Named casts and the one sanctioned static
// downcast per E-865; use dynamic_cast directly where the type is really open.
template <typename Derived, typename Base>
[[nodiscard]] Derived *qcDowncast(Base *base)
{
    Q_ASSERT(!base || dynamic_cast<Derived *>(base));
    return static_cast<Derived *>(base); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast): checked above in debug builds
}
