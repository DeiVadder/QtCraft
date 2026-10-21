#include "server/blockentities/QcSignTexts.h"

#include <algorithm>

QcSignTexts::QcSignTexts(const QcContentRegistry &content)
{
    const auto count = std::size_t(content.stateCount());
    m_sign.resize(count);
    for (std::size_t id = 0; id < count; ++id) {
        m_sign[id] = content.block(QcBlockId(id)).sign;
    }
}

void QcSignTexts::noteBlock(QcBlockPos pos, QcBlockId after)
{
    if (!isSign(after)) {
        remove(pos);
    }
}

void QcSignTexts::forgetChunk(QcChunkPos pos)
{
    const QList<QcBlockPos> positions = m_byChunk.take(pos);
    for (const QcBlockPos &at : positions) {
        m_entries.remove(at);
    }
}

QList<QcSignTexts::Entry> QcSignTexts::savedOf(QcChunkPos pos) const
{
    QList<QcBlockPos> positions = m_byChunk.value(pos);
    std::ranges::sort(positions);
    QList<Entry> entries;
    entries.reserve(positions.size());
    for (const QcBlockPos &at : std::as_const(positions)) {
        const QcSignText::Text text = m_entries.value(at);
        entries.append({at, text.lines, text.color});
    }
    return entries;
}

QList<QcSignTexts::Entry> QcSignTexts::takeChunk(QcChunkPos pos)
{
    QList<Entry> entries = savedOf(pos);
    forgetChunk(pos);
    return entries;
}

void QcSignTexts::restore(const QList<Entry> &entries, const QcChunk &chunk)
{
    for (const Entry &entry : entries) {
        if (entry.pos.chunk() != chunk.pos()) {
            continue;
        }
        const QcBlockId block = chunk.block(entry.pos.localX(), entry.pos.y, entry.pos.localZ());
        if (isSign(block)) {
            const QcSignDefinition &sign = m_sign[std::size_t(block)];
            const QString color = QcSignText::colorOfDye(entry.color).value_or(QString());
            put(entry.pos, {QcSignText::cleaned(entry.lines, sign.lines, sign.lineLength), color});
        }
    }
}

void QcSignTexts::set(QcBlockPos pos, const Lines &lines)
{
    put(pos, {lines, colorAt(pos)});
}

void QcSignTexts::setColor(QcBlockPos pos, const QString &color)
{
    const Lines *lines = at(pos);
    put(pos, {lines ? *lines : Lines{}, color});
}

void QcSignTexts::put(QcBlockPos pos, const QcSignText::Text &text)
{
    if (QcSignText::isBlank(text)) {
        remove(pos);
        return;
    }

    const auto it = m_entries.find(pos);
    if (it != m_entries.end()) {
        if (*it != text) {
            *it = text;
            m_changes.append(pos);
        }
        return;
    }
    m_entries.insert(pos, text);
    m_byChunk[pos.chunk()].append(pos);
    m_changes.append(pos);
}

const QcSignTexts::Lines *QcSignTexts::at(QcBlockPos pos) const
{
    const auto it = m_entries.constFind(pos);
    return it != m_entries.cend() ? &it->lines : nullptr;
}

QString QcSignTexts::colorAt(QcBlockPos pos) const
{
    const auto it = m_entries.constFind(pos);
    return it != m_entries.cend() ? it->color : QString();
}

QList<QcBlockPos> QcSignTexts::takeChanges()
{
    QList<QcBlockPos> changes = std::move(m_changes);
    m_changes.clear();
    std::ranges::sort(changes);
    const auto [first, last] = std::ranges::unique(changes);
    changes.erase(first, last);
    return changes;
}

void QcSignTexts::clear()
{
    m_entries.clear();
    m_byChunk.clear();
    m_changes.clear();
}

void QcSignTexts::remove(QcBlockPos pos)
{
    if (!m_entries.remove(pos)) {
        return;
    }
    const auto chunk = m_byChunk.find(pos.chunk());
    if (chunk != m_byChunk.end()) {
        chunk->removeOne(pos);
        if (chunk->isEmpty()) {
            m_byChunk.erase(chunk);
        }
    }
    m_changes.append(pos);
}
