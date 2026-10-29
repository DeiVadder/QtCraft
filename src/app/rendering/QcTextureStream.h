// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QList>
#include <rhi/qrhi.h>

#include <memory>
#include <utility>

// A mipmapped RGBA8 texture uploaded a few rows at a time (decision E-912). QRhi copies every
// upload into a staging buffer, and an atlas uploaded at once needs one as large as all its levels,
// which the process keeps resident; a budget of bytes per frame keeps the staging copies small.
// The texture is bound only once done(). Render thread only.
class QcTextureStream
{
public:
    // Creates the texture for `levels` (level 0 first) and the smaller levels down to 1 x 1
    // (QcAtlasTexture::chain); false when the GPU refuses it. The images are shared, not copied.
    [[nodiscard]] bool start(QRhi *rhi, const QList<QImage> &levels);
    // Uploads the next rows with `updates`: at most `budget` bytes, but at least one row. Returns
    // the bytes taken.
    qint64 advance(QRhiResourceUpdateBatch *updates, qint64 budget);
    // Whether every level is uploaded (also when nothing was started).
    [[nodiscard]] bool done() const { return next().level >= chain().size(); }
    // Size of the texture's level 0 (empty before start() and after take()).
    [[nodiscard]] QSize size() const { return chain().isEmpty() ? QSize() : chain().front().size(); }
    // Bytes of all levels.
    [[nodiscard]] qint64 bytes() const;
    // The texture once done(); the stream is empty afterwards.
    [[nodiscard]] std::unique_ptr<QRhiTexture> take();

private:
    // The next rows to upload: their level and first row.
    struct Position
    {
        qsizetype level = 0;
        int row = 0;
    };

    [[nodiscard]] QRhiTexture *texture() const { return m_texture.get(); }
    [[nodiscard]] const QList<QImage> &chain() const { return m_chain; }
    void setChain(QList<QImage> chain) { m_chain = std::move(chain); }
    [[nodiscard]] Position next() const { return m_next; }
    void setNext(Position next) { m_next = next; }

    std::unique_ptr<QRhiTexture> m_texture;
    // The levels to upload; their images are let go with take().
    QList<QImage> m_chain;
    Position m_next;
};
