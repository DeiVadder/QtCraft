#include "QcCrc32.h"

#include <array>

namespace {

constexpr std::array<quint32, 256> makeTable()
{
    std::array<quint32, 256> table{};
    for (quint32 i = 0; i < 256; ++i) {
        quint32 value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) != 0 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
        }
        table[i] = value;
    }
    return table;
}

constexpr std::array<quint32, 256> crcTable = makeTable();

} // namespace

quint32 qcCrc32(QByteArrayView data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : data) {
        crc = crcTable[(crc ^ quint8(byte)) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}
