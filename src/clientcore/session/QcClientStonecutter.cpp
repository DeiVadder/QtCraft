#include "clientcore/session/QcClientStonecutter.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcStonecutting.h"

bool QcClientStonecutter::handle(const QcMessage &message)
{
    if (std::holds_alternative<QcMsg::CloseStonecutter>(message)) {
        if (m_open) {
            m_open = false;
            emit closed();
        }
        return true;
    }

    const auto *open = std::get_if<QcMsg::OpenStonecutter>(&message);
    if (!open) {
        return false;
    }
    m_open = true;
    m_pos = open->pos;
    emit opened();
    return true;
}

bool QcClientStonecutter::cut(const QString &recipe, bool repeat)
{
    if (!m_open || !m_send) {
        return false;
    }
    m_send(QcMsg::CutStone{recipe, repeat});
    return true;
}

void QcClientStonecutter::close()
{
    if (!m_open) {
        return;
    }
    m_open = false;
    if (m_send) {
        m_send(QcMsg::CloseStonecutter{});
    }
    emit closed();
}

QStringList QcClientStonecutter::inputs(const QcContentRegistry &content, const QList<QcItemStack> &inventory)
{
    QStringList found;
    for (const QcItemStack &stack : inventory) {
        if (stack.isEmpty() || !content.isValidItem(stack.item)) {
            continue;
        }
        const QString &name = content.item(stack.item).name;
        if (!found.contains(name) && !QcStonecutting::recipesFor(content, name).isEmpty()) {
            found.append(name);
        }
    }
    return found;
}
