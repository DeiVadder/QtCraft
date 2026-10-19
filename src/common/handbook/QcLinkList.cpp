#include "common/handbook/QcLinkList.h"

#include "common/handbook/QcHandbookLinks.h"

using namespace Qt::StringLiterals;

void QcLinkList::addUnique(Link link)
{
    if (link.target.isEmpty()) {
        m_links.append(std::move(link));
        return;
    }

    qsizetype &position = m_positionOfTarget[link.target];
    if (position == 0) {
        m_links.append(std::move(link));
        position = m_links.size();
        return;
    }

    Link &same = m_links[position - 1];
    if (!link.note.isEmpty() && !same.note.contains(link.note)) {
        same.note = same.note.isEmpty() ? link.note : same.note + u", "_s + link.note;
    }
}

QList<QcLinkList::Link> QcLinkList::sorted() const
{
    return QcHandbookLinks::sortedLinks(m_links);
}
