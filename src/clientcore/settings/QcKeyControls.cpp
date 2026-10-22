#include "clientcore/settings/QcKeyControls.h"

#include "clientcore/settings/QcKeyLabels.h"

#include <QStringList>

using namespace Qt::StringLiterals;

QcKeyControls::QcKeyControls(QObject *parent)
    : QObject(parent)
{
}

void QcKeyControls::setBindings(const QcKeyBindings &bindings)
{
    if (m_bindings != bindings) {
        m_bindings = bindings;
        emit changed();
    }
}

QVariantList QcKeyControls::rows() const
{
    QVariantList rows;
    for (const QcKeyBindings::Action action : QcKeyBindings::actions()) {
        rows.append(QVariantMap{{u"action"_s, QcKeyBindings::id(action)},
                                {u"label"_s, QcKeyLabels::action(action)},
                                {u"primary"_s, QcKeyLabels::key(m_bindings.key(action, 0))},
                                {u"secondary"_s, QcKeyLabels::key(m_bindings.key(action, 1))},
                                {u"fixed"_s, QcKeyBindings::isFixed(action)}});
    }
    return rows;
}

QVariantMap QcKeyControls::labels() const
{
    QVariantMap labels;
    for (const QcKeyBindings::Action action : QcKeyBindings::actions()) {
        const QList<int> keys = m_bindings.keys(action);
        const int first = keys.isEmpty() ? 0 : keys.first();
        // A Mac needs fn for the function keys (F11 for fullscreen is a secondary key there).
        const bool fn = m_bindings.platform() == QcKeyBindings::Platform::Mac && QcKeyBindings::isFunctionKey(first);
        labels.insert(QcKeyBindings::id(action), fn ? u"fn+"_s + QcKeyLabels::key(first) : QcKeyLabels::key(first));
    }
    return labels;
}

bool QcKeyControls::matches(const QString &action, int key) const
{
    const std::optional<QcKeyBindings::Action> found = QcKeyBindings::actionFromId(action);
    return found && m_bindings.matches(*found, key);
}

QString QcKeyControls::bind(const QString &action, int slot, int key)
{
    const std::optional<QcKeyBindings::Action> found = QcKeyBindings::actionFromId(action);
    if (!found || QcKeyBindings::isFixed(*found)) {
        return {};
    }

    const bool mac = m_bindings.platform() == QcKeyBindings::Platform::Mac;
    if (key == QcKeyBindings::clearKey) {
        key = 0;
    } else if (mac && key == Qt::Key_Control) {
        return tr("Cmd cannot be used on a Mac: macOS keeps Cmd+Q, Cmd+H, Cmd+Space and other Cmd shortcuts for itself.");
    } else if (!QcKeyBindings::isBindable(key, m_bindings.platform())) {
        return tr("This key cannot be used.");
    }

    const QcKeyBindings before = m_bindings;
    const std::optional<QcKeyBindings::Displaced> displaced = m_bindings.bind(*found, slot, key);
    if (m_bindings == before) {
        return {};
    }

    emit changed();
    emit edited();

    QStringList notes;
    if (displaced) {
        const QString other = QcKeyLabels::action(displaced->action);
        notes.append(displaced->key == 0 ? tr("%1 was used for “%2”, which now has no key there.").arg(QcKeyLabels::key(key), other)
                                         : tr("%1 was used for “%2”, which now uses %3.")
                                               .arg(QcKeyLabels::key(key), other, QcKeyLabels::key(displaced->key)));
    }
    if (mac && key == Qt::Key_Meta) {
        notes.append(tr("Careful: on a Mac, Control with the arrow keys or Space switches desktops or the input source."));
    }
    return notes.join(u' ');
}

void QcKeyControls::reset()
{
    if (m_bindings != QcKeyBindings::defaults()) {
        m_bindings = QcKeyBindings::defaults();
        emit changed();
        emit edited();
    }
}
