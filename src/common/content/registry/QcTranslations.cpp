#include "common/content/registry/QcTranslations.h"

void QcTranslations::add(const QString &language, const QHash<QString, QString> &entries)
{
    QHash<QString, QString> &table = m_tables[language];
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        table.insert(it.key(), it.value());
    }
}

QString QcTranslations::translate(const QString &key, const QString &language) const
{
    for (const QString &candidate : {language, QString::fromLatin1(fallbackLanguage)}) {
        const auto table = m_tables.constFind(candidate);
        if (table != m_tables.constEnd()) {
            const auto entry = table->constFind(key);
            if (entry != table->constEnd()) {
                return *entry;
            }
        }
    }
    return key;
}

qsizetype QcTranslations::total() const
{
    qsizetype total = 0;
    for (const QHash<QString, QString> &table : m_tables) {
        total += table.size();
    }
    return total;
}

QStringList QcTranslations::keys(const QString &language) const
{
    QStringList keys = m_tables.value(language).keys();
    keys.sort();
    return keys;
}

bool QcTranslations::has(const QString &key, const QString &language) const
{
    const auto table = m_tables.constFind(language);
    return table != m_tables.constEnd() && table->contains(key);
}
