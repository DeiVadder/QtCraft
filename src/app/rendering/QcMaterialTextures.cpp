// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcMaterialTextures.h"

#include "app/debug/QcAppLogging.h"
#include "clientcore/materials/QcMaterialAtlas.h"
#include "clientcore/materials/QcMaterialParams.h"
#include "clientcore/materials/QcMaterialSynthesizer.h"
#include "clientcore/textures/QcAtlasLayout.h"

#include <algorithm>

#include <utility>

namespace {

std::unique_ptr<QRhiTexture> flatTexture(QRhi *rhi, QColor texel, QRhiResourceUpdateBatch *updates)
{
    std::unique_ptr<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!texture->create()) {
        return nullptr;
    }
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(texel);
    updates->uploadTexture(texture.get(), image);
    return texture;
}

} // namespace

void QcMaterialTextures::offer(quint64 serial, std::shared_ptr<const QcMaterialAtlas> pixels)
{
    if (pixels) {
        setCurrent(pixels->serial());
        setPixels(std::move(pixels));
        return;
    }
    // The same set again; another one without pixels (none) lets the held ones go.
    if (serial != current()) {
        setCurrent(serial);
        setPixels(nullptr);
    }
}

quint64 QcMaterialTextures::target(QRhi *rhi, bool wanted) const
{
    if (!wanted || current() == 0 || refused() == Request{rhi, current()}) {
        return 0;
    }
    if (m_gpu.rhi == rhi && m_gpu.serial == current()) {
        return current();
    }
    return pixels() && !pixels()->normalLevels().isEmpty() ? current() : 0;
}

bool QcMaterialTextures::replaces(QRhi *rhi, bool wanted) const
{
    if (!holdsResources()) {
        return false;
    }
    if (m_gpu.rhi != rhi || (m_gpu.normal && !fits(m_gpu.cell))) {
        return true;
    }
    const quint64 next = target(rhi, wanted);
    // A set on its way to the GPU replaces the bound textures only once it is uploaded.
    return m_gpu.serial != next && (next == 0 || (m_gpu.uploading == next && uploaded()));
}

bool QcMaterialTextures::prepare(QRhi *rhi, bool wanted, QRhiResourceUpdateBatch *updates)
{
    if (m_gpu.rhi != rhi) {
        release();
    }
    if (!usable(rhi)) {
        // No maps can be drawn on this QRhi: nothing waits for an upload (takeLost stays quiet).
        setPixels(nullptr);
        return false;
    }
    if (!m_gpu.flatNormal && !createFlat(rhi, updates)) {
        setRefused(Request{rhi, 0});
        setPixels(nullptr);
        return false;
    }
    // Maps of another cell size than the colour atlas (replaces() saw it) are not drawn.
    if (m_gpu.normal && !fits(m_gpu.cell)) {
        dropMaps();
    }

    const quint64 wantedMaps = target(rhi, wanted);
    if (m_gpu.serial == wantedMaps || wantedMaps == 0) {
        if (m_gpu.serial != wantedMaps) {
            dropMaps();
        } else if (wantedMaps != 0) {
            // The same set offered again while on the GPU: no copy is kept.
            setPixels(nullptr);
        }
        dropUpload();
    } else if (m_gpu.uploading != wantedMaps) {
        startUpload(wantedMaps, updates);
    } else if (!uploaded()) {
        streamUpload(updates);
    } else {
        bindUpload();
    }
    return true;
}

void QcMaterialTextures::startUpload(quint64 serial, QRhiResourceUpdateBatch *updates)
{
    dropUpload();
    const QList<QImage> &normalLevels = pixels()->normalLevels();
    if (!m_gpu.normalUpload.start(m_gpu.rhi, normalLevels) || !m_gpu.specularUpload.start(m_gpu.rhi, pixels()->specularLevels())) {
        // The bound textures go with the next frame's replaces(), which sees the refusal.
        refuse(serial, "the GPU refused the maps");
        return;
    }
    m_gpu.uploading = serial;
    // The cells of the texture made (the GPU may have taken smaller levels only).
    m_gpu.uploadingCell = m_gpu.normalUpload.size().width() / QcAtlasLayout::gridOf(normalLevels.back().size()).width();
    streamUpload(updates);
}

void QcMaterialTextures::streamUpload(QRhiResourceUpdateBatch *updates)
{
    const qint64 taken = m_gpu.normalUpload.advance(updates, uploadBudget());
    if (taken < uploadBudget()) {
        m_gpu.specularUpload.advance(updates, uploadBudget() - taken);
    }
}

bool QcMaterialTextures::uploaded() const
{
    return m_gpu.normalUpload.done() && m_gpu.specularUpload.done();
}

void QcMaterialTextures::bindUpload()
{
    dropMaps();
    if (!fits(m_gpu.uploadingCell)) {
        refuse(std::exchange(m_gpu.uploading, 0), "the maps' cells differ from the colour atlas's");
        return;
    }
    m_gpu.cell = m_gpu.uploadingCell;
    m_gpu.bytes = m_gpu.normalUpload.bytes() + m_gpu.specularUpload.bytes();
    m_gpu.normal = m_gpu.normalUpload.take();
    m_gpu.specular = m_gpu.specularUpload.take();
    m_gpu.serial = std::exchange(m_gpu.uploading, 0);
    // On the GPU now: no copy is kept.
    setPixels(nullptr);
}

bool QcMaterialTextures::fits(int cell) const
{
    // The block shader reads at most the cells of maxTileSize (block.frag, surfaceAt).
    return m_atlasCell == 0 || cell == std::min(m_atlasCell, QcAtlasLayout::cellSize(QcMaterialAtlas::maxTileSize));
}

void QcMaterialTextures::refuse(quint64 serial, const char *why)
{
    const QSize size = pixels() ? pixels()->normalLevels().front().size() : QSize();
    qCWarning(lcRenderer) << "Material maps:" << why << "for maps of" << size << "; the realistic mode draws flat surfaces";
    dropUpload();
    setRefused(Request{m_gpu.rhi, serial});
    // Not tried again on this QRhi: nothing is kept for it (a new QRhi reports the set lost).
    setPixels(nullptr);
}

void QcMaterialTextures::dropUpload()
{
    m_gpu.uploading = 0;
    m_gpu.uploadingCell = 0;
    const std::unique_ptr<QRhiTexture> normal = m_gpu.normalUpload.take();
    const std::unique_ptr<QRhiTexture> specular = m_gpu.specularUpload.take();
}

bool QcMaterialTextures::takeLost(bool wanted)
{
    // A refused set, or none at all when even the flat textures were refused.
    const bool refusedSet = refused() && (refused()->maps == current() || refused()->maps == 0);
    const bool lost = wanted && current() != 0 && !pixels() && m_gpu.serial != current() && !refusedSet && m_reportedLost != current();
    if (lost) {
        m_reportedLost = current();
    }
    return lost;
}

bool QcMaterialTextures::createFlat(QRhi *rhi, QRhiResourceUpdateBatch *updates)
{
    // Uploaded in a batch of their own that joins `updates` only when everything was made.
    QRhiResourceUpdateBatch *const own = rhi->nextResourceUpdateBatch();
    Resources made;
    made.rhi = rhi;
    made.sampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::Linear, QRhiSampler::ClampToEdge,
                                       QRhiSampler::ClampToEdge));
    // The specular map's codes (metals 230..237 beside dielectric F0) must not blend between mip
    // levels either: its levels are picked, not mixed (decision E-908).
    made.specularSampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::ClampToEdge,
                                               QRhiSampler::ClampToEdge));
    const bool samplerMade = made.sampler->create() && made.specularSampler->create();
    made.flatNormal = samplerMade ? flatTexture(rhi, QColor::fromRgba(QcMaterialSynthesizer::flatNormal), own) : nullptr;
    made.flatSpecular = made.flatNormal ? flatTexture(rhi, QcMaterialSynthesizer::specularTexel(QcMaterialParams::flat(), 0.0f), own) : nullptr;
    if (!made.flatSpecular) {
        own->release();
        qCWarning(lcRenderer) << "Material maps: the GPU refused the flat textures";
        return false;
    }
    updates->merge(own);
    own->release();
    m_gpu = std::move(made);
    return true;
}

bool QcMaterialTextures::usable(QRhi *rhi) const
{
    return refused() != Request{rhi, 0};
}

QList<QRhiShaderResourceBinding> QcMaterialTextures::entries() const
{
    if (!m_gpu.flatNormal) {
        return {};
    }
    constexpr auto fragment = QRhiShaderResourceBinding::FragmentStage;
    QRhiTexture *const normal = m_gpu.normal ? m_gpu.normal.get() : m_gpu.flatNormal.get();
    QRhiTexture *const specular = m_gpu.specular ? m_gpu.specular.get() : m_gpu.flatSpecular.get();
    return {QRhiShaderResourceBinding::sampledTexture(normalBinding, fragment, normal, m_gpu.sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(specularBinding, fragment, specular, m_gpu.specularSampler.get())};
}

void QcMaterialTextures::dropMaps()
{
    m_gpu.specular.reset();
    m_gpu.normal.reset();
    m_gpu.serial = 0;
    m_gpu.bytes = 0;
    m_gpu.cell = 0;
}

void QcMaterialTextures::standBy(bool keepTextures)
{
    dropUpload();
    setPixels(nullptr);
    if (!keepTextures) {
        release();
    }
}

void QcMaterialTextures::release()
{
    // Handed out whole, so its destructor releases the members in reverse order.
    const Resources retired = std::exchange(m_gpu, {});
}

void QcMaterialTextures::reset()
{
    release();
    setRefused(std::nullopt);
}
