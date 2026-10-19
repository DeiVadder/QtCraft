#include "server/storage/QcStackReader.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcStackReader::QcStackReader(const QcContentRegistry &content, QString path, QcLoadWarnings &warnings)
    : m_content(content)
    , m_path(std::move(path))
    , m_warnings(warnings)
{
}

QcItemStack QcStackReader::read(QDataStream &stream)
{
    QString name;
    qint32 itemCount = 0;
    qint32 damage = 0;
    stream >> name >> itemCount >> damage;
    if (name.isEmpty() || itemCount <= 0) {
        return {};
    }

    const std::optional<QcItemId> item = m_content.itemId(name);
    if (!item) {
        m_warnings.addOnce(u"item:"_s + name, u"%1: unknown item '%2' dropped (in this world reported only here)"_s.arg(m_path, name));
        return {};
    }
    const QcItemDefinition &definition = m_content.item(*item);
    return {*item, std::clamp(itemCount, 1, definition.maxStack), std::clamp(damage, 0, std::max(0, definition.durability - 1))};
}
