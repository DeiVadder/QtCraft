#include "app/debug/QcGraphItem.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>

#include <algorithm>

namespace {

// A line strip node with its own geometry and colour.
QSGGeometryNode *lineNode(const QColor &color)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawLineStrip);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

void recolor(QSGGeometryNode *node, const QColor &color)
{
    auto *material = static_cast<QSGFlatColorMaterial *>(node->material());
    if (material->color() != color) {
        material->setColor(color);
        node->markDirty(QSGNode::DirtyMaterial);
    }
}

} // namespace

QcGraphItem::QcGraphItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void QcGraphItem::changeAndUpdate()
{
    emit changed();
    update();
}

void QcGraphItem::setValues(const QList<qreal> &values)
{
    m_values = values;
    changeAndUpdate();
}

void QcGraphItem::setCapacity(int capacity)
{
    if (capacity >= 2 && capacity != m_capacity) {
        m_capacity = capacity;
        changeAndUpdate();
    }
}

void QcGraphItem::setMaxValue(qreal value)
{
    if (value > 0.0 && value != m_maxValue) {
        m_maxValue = value;
        changeAndUpdate();
    }
}

void QcGraphItem::setLimit(qreal value)
{
    if (value != m_limit) {
        m_limit = value;
        changeAndUpdate();
    }
}

void QcGraphItem::setColor(const QColor &color)
{
    if (color != m_color) {
        m_color = color;
        changeAndUpdate();
    }
}

void QcGraphItem::setLimitColor(const QColor &color)
{
    if (color != m_limitColor) {
        m_limitColor = color;
        changeAndUpdate();
    }
}

QSGNode *QcGraphItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *data)
{
    Q_UNUSED(data);
    auto *root = old ? old : new QSGNode;
    if (!old) {
        root->appendChildNode(lineNode(m_color));
        root->appendChildNode(lineNode(m_limitColor));
    }
    auto *line = static_cast<QSGGeometryNode *>(root->firstChild());
    auto *limitLine = static_cast<QSGGeometryNode *>(root->lastChild());
    recolor(line, m_color);
    recolor(limitLine, m_limitColor);

    // The newest values, right-aligned.
    const qsizetype shown = std::min<qsizetype>(m_values.size(), m_capacity);
    const qreal step = width() / qreal(m_capacity - 1);
    const qreal left = width() - step * qreal(shown - 1);
    const auto yOf = [this](qreal value) { return float(height() - std::clamp(value / m_maxValue, 0.0, 1.0) * height()); };

    QSGGeometry *geometry = line->geometry();
    geometry->allocate(int(shown));
    QSGGeometry::Point2D *points = geometry->vertexDataAsPoint2D();
    for (qsizetype i = 0; i < shown; ++i) {
        points[i].set(float(left + step * qreal(i)), yOf(m_values[m_values.size() - shown + i]));
    }
    line->markDirty(QSGNode::DirtyGeometry);

    // A limit above the scale would lie on the top edge, where it looks like a value reaching it.
    QSGGeometry *limitGeometry = limitLine->geometry();
    limitGeometry->allocate(limitShown() ? 2 : 0);
    if (limitShown()) {
        limitGeometry->vertexDataAsPoint2D()[0].set(0.0f, yOf(m_limit));
        limitGeometry->vertexDataAsPoint2D()[1].set(float(width()), yOf(m_limit));
    }
    limitLine->markDirty(QSGNode::DirtyGeometry);
    return root;
}
