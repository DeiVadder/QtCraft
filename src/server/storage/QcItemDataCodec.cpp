#include "server/storage/QcItemDataCodec.h"

#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcSaveFile.h"

namespace QcItemDataCodec {

void write(QDataStream &stream, const QcItemData &data, const QcContentRegistry &content)
{
    QcComponentFrames::Writer components(stream, data.kept);
    if (data.mapId != 0) {
        components.add(quint16(QcItemData::Component::MapId), version, [&data](QDataStream &out) { out << data.mapId; });
    }
    if (!data.contents.isEmpty()) {
        components.add(quint16(QcItemData::Component::Contents), version, [&data, &content](QDataStream &out) {
            out << quint16(data.contents.size());
            for (const QcItemStack &slot : data.contents) {
                QcSaveFile::writeStack(out, slot, content);
            }
        });
    }
    if (!data.name.isEmpty()) {
        components.add(quint16(QcItemData::Component::Name), version, [&data](QDataStream &out) { out << data.name; });
    }
    components.finish();
}

} // namespace QcItemDataCodec
