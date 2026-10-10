#include "QcFrameProfile.h"

#include <algorithm>

using namespace Qt::StringLiterals;

void QcFrameProfile::clear()
{
    m_guiCpuMs.clear();
    m_entityMeshMs.clear();
    m_previewMeshMs.clear();
    m_render.clear();
}

void QcFrameProfile::addGuiFrame(double guiCpuMs, double entityMeshMs, double previewMeshMs)
{
    if (guiCpuMs >= 0.0) {
        m_guiCpuMs.append(guiCpuMs);
    }
    m_entityMeshMs.append(entityMeshMs);
    m_previewMeshMs.append(previewMeshMs);
}

void QcFrameProfile::addRenderSamples(const QList<QcRenderSample> &samples)
{
    m_render.append(samples);
}

double QcFrameProfile::average(const QList<double> &values)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    return total / double(values.size());
}

double QcFrameProfile::percentile99(QList<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    std::ranges::sort(values);
    return values[std::min(values.size() - 1, values.size() * 99 / 100)];
}

QString QcFrameProfile::summary() const
{
    QList<double> renderCpu;
    QList<double> worldRender;
    QList<double> upload;
    QList<double> gpu;
    QList<double> drawCalls;
    QList<double> visible;
    QList<double> triangles;
    double uploadBytes = 0.0;

    for (const QcRenderSample &sample : m_render) {
        if (sample.renderThreadCpuMs >= 0.0) {
            renderCpu.append(sample.renderThreadCpuMs);
        }
        worldRender.append(sample.worldRenderMs);
        upload.append(sample.uploadMs);
        if (sample.gpuMs > 0.0) {
            gpu.append(sample.gpuMs);
        }
        drawCalls.append(sample.drawCalls);
        visible.append(sample.visibleSections);
        triangles.append(double(sample.indicesDrawn) / 3.0);
        uploadBytes += double(sample.uploadBytes);
    }

    const QcRenderSample last = m_render.isEmpty() ? QcRenderSample{} : m_render.constLast();
    const auto ms = [](double value) { return QString::number(value, 'f', 3); };
    return u"gui_cpu_avg_ms=%1 gui_cpu_p99_ms=%2 render_cpu_avg_ms=%3 render_cpu_p99_ms=%4 world_render_avg_ms=%5 "
           u"world_render_p99_ms=%6 upload_avg_ms=%7 upload_max_ms=%8 upload_mb=%9 gpu_avg_ms=%10 gpu_p99_ms=%11 "
           u"entity_mesh_avg_ms=%12 entity_mesh_p99_ms=%13 draw_calls=%14 visible_sections=%15 triangles=%16 "
           u"sections=%17 section_mb=%18 render_frames=%19 preview_mesh_avg_ms=%20 preview_mesh_max_ms=%21"_s
        .arg(ms(average(m_guiCpuMs)), ms(percentile99(m_guiCpuMs)), ms(average(renderCpu)), ms(percentile99(renderCpu)),
             ms(average(worldRender)), ms(percentile99(worldRender)), ms(average(upload)),
             ms(upload.isEmpty() ? 0.0 : *std::ranges::max_element(upload)), QString::number(uploadBytes / 1e6, 'f', 1))
        .arg(ms(average(gpu)), ms(percentile99(gpu)), ms(average(m_entityMeshMs)), ms(percentile99(m_entityMeshMs)),
             QString::number(average(drawCalls), 'f', 0), QString::number(average(visible), 'f', 0),
             QString::number(average(triangles), 'f', 0), QString::number(last.sections),
             QString::number(double(last.sectionBytes) / 1e6, 'f', 1))
        .arg(QString::number(m_render.size()), ms(average(m_previewMeshMs)),
             ms(m_previewMeshMs.isEmpty() ? 0.0 : *std::ranges::max_element(m_previewMeshMs)));
}
