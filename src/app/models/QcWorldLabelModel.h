#pragma once

#include "QcLabelProjector.h"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

class QcContentRegistry;

// The world labels drawn this frame for QML (GameController.hud.labels, decision E-222): translated
// title and detail, screen position, size, opacity and the widest it may be drawn. Rows are rebuilt only when the set of
// labels changes; moving the camera only changes positions.
class QcWorldLabelModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role { TitleRole = Qt::UserRole + 1, DetailRole, HeadingRole, XRole, YRole, ScaleRole, OpacityRole, MaxWidthRole };

    explicit QcWorldLabelModel(QObject *parent = nullptr);

    // `labels` as QcLabelProjector::visible() returns them; texts are translated with `content`.
    void update(const QList<QcScreenLabel> &labels, const QcContentRegistry &content, const QString &language);
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Row
    {
        QcScreenLabel label;
        QString title;
    };

    QList<Row> m_rows;
    QString m_language;
};
