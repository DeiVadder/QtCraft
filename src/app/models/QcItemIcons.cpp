#include "QcItemIcons.h"

#include "QcItemIconProvider.h"
#include "QcItemIconSet.h"

void QcItemIcons::draw(const QcContentRegistry &content, const QcTextureAtlas &atlas, const QcBlockRenderTable *renderTable)
{
    m_ready = true;
    if (!m_provider) {
        return;
    }

    m_provider->setIcons(QcItemIconSet::draw(content, atlas, renderTable));
}
