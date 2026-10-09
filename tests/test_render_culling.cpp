#include "QcCamera.h"
#include "QcFrustum.h"
#include "QcIndexPacking.h"

#include <QTest>

#include <cmath>
#include <cstring>
#include <numbers>

namespace {

constexpr float aspect = 16.0f / 9.0f;
constexpr float farPlane = 128.0f;
constexpr float eyeY = 64.0f;

// Clip-space corrections as QRhi applies them: none (OpenGL), depth to [0, 1] (Metal, D3D) and
// depth to [0, 1] with y flipped (Vulkan).
QMatrix4x4 correction(int kind)
{
    QMatrix4x4 matrix;
    if (kind >= 1) {
        matrix = QMatrix4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5f, 0.5f, 0, 0, 0, 1);
    }
    if (kind == 2) {
        matrix = QMatrix4x4(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) * matrix;
    }
    return matrix;
}

QcFrustum::Planes planesFor(const QcCamera &camera, int kind)
{
    return QcFrustum::planes(correction(kind) * camera.projectionMatrix(aspect, farPlane) * camera.viewMatrix());
}

// Half-extent of the view at `distance` in front of the camera, from the field of view alone.
float halfHeightAt(float distance)
{
    return distance * std::tan(QcCamera::defaultFieldOfView / 2.0f * std::numbers::pi_v<float> / 180.0f);
}

QcCamera cameraAtOrigin(float yaw = 0.0f)
{
    QcCamera camera;
    camera.position = QVector3D(0.0f, eyeY, 0.0f);
    camera.yaw = yaw;
    return camera;
}

bool visible(const QcFrustum::Planes &planes, QVector3D low, QVector3D high)
{
    return QcFrustum::intersectsBox(planes, low, high);
}

} // namespace

class TestRenderCulling : public QObject
{
    Q_OBJECT

private slots:
    void boxes_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("opengl") << 0;
        QTest::newRow("metal") << 1;
        QTest::newRow("vulkan") << 2;
    }

    void boxes()
    {
        QFETCH(int, kind);
        const QcFrustum::Planes planes = planesFor(cameraAtOrigin(), kind);
        // The camera looks along -Z.
        QVERIFY(visible(planes, {-0.5f, eyeY - 0.5f, -20.5f}, {0.5f, eyeY + 0.5f, -19.5f}));
        // Behind the camera, also when the box is far wider and taller than the view.
        QVERIFY(!visible(planes, {-0.5f, eyeY - 0.5f, 19.5f}, {0.5f, eyeY + 0.5f, 20.5f}));
        QVERIFY(!visible(planes, {-100.0f, 0.0f, 5.0f}, {100.0f, 128.0f, 10.0f}));
        // A section containing the camera.
        QVERIFY(visible(planes, {-8.0f, 56.0f, -8.0f}, {8.0f, 72.0f, 8.0f}));

        // Right, left, top and bottom screen edges; the boxes reach 20 m ahead at most, where the
        // view is widest for them.
        const float halfHeight = halfHeightAt(20.0f);
        const float halfWidth = halfHeight * aspect;
        QVERIFY(visible(planes, {halfWidth - 1.5f, eyeY, -20.0f}, {halfWidth - 0.5f, eyeY + 1.0f, -19.5f}));
        QVERIFY(visible(planes, {halfWidth - 0.5f, eyeY, -20.0f}, {halfWidth + 0.5f, eyeY + 1.0f, -19.5f}));
        QVERIFY(!visible(planes, {halfWidth + 0.5f, eyeY, -20.0f}, {halfWidth + 1.5f, eyeY + 1.0f, -19.5f}));
        QVERIFY(!visible(planes, {-halfWidth - 1.5f, eyeY, -20.0f}, {-halfWidth - 0.5f, eyeY + 1.0f, -19.5f}));
        QVERIFY(visible(planes, {0.0f, eyeY + halfHeight - 0.5f, -20.0f}, {1.0f, eyeY + halfHeight + 0.5f, -19.5f}));
        QVERIFY(!visible(planes, {0.0f, eyeY + halfHeight + 0.5f, -20.0f}, {1.0f, eyeY + halfHeight + 1.5f, -19.5f}));
        QVERIFY(!visible(planes, {0.0f, eyeY - halfHeight - 1.5f, -20.0f}, {1.0f, eyeY - halfHeight - 0.5f, -19.5f}));

        // Far plane.
        QVERIFY(visible(planes, {-1.0f, eyeY, -farPlane - 2.0f}, {1.0f, eyeY + 1.0f, -farPlane + 2.0f}));
        QVERIFY(!visible(planes, {-1.0f, eyeY, -farPlane - 6.0f}, {1.0f, eyeY + 1.0f, -farPlane - 2.0f}));
    }

    void turnedCamera()
    {
        // Yaw 90 looks along +X.
        const QcFrustum::Planes planes = planesFor(cameraAtOrigin(90.0f), 1);
        QVERIFY(visible(planes, {19.5f, eyeY - 0.5f, -0.5f}, {20.5f, eyeY + 0.5f, 0.5f}));
        QVERIFY(!visible(planes, {-20.5f, eyeY - 0.5f, -0.5f}, {-19.5f, eyeY + 0.5f, 0.5f}));
        QVERIFY(!visible(planes, {-0.5f, eyeY - 0.5f, -40.5f}, {0.5f, eyeY + 0.5f, -39.5f}));
    }

    void shortIndicesUpTo65536Vertices()
    {
        QVERIFY(QcIndexPacking::fitsShortIndices(0));
        QVERIFY(QcIndexPacking::fitsShortIndices(4));
        // Indices 0 .. 65535 still fit into 16 bits.
        QVERIFY(QcIndexPacking::fitsShortIndices(65536));
        QVERIFY(!QcIndexPacking::fitsShortIndices(65537));
        QVERIFY(!QcIndexPacking::fitsShortIndices(200000));
    }

    void shortIndicesKeepValuesAndOrder()
    {
        const QList<quint32> indices = {0, 1, 2, 2, 3, 0, 65535, 40000};
        const QByteArray packed = QcIndexPacking::toShortIndices(indices);
        QCOMPARE(packed.size(), qsizetype(16));
        std::array<quint16, 8> values{};
        std::memcpy(values.data(), packed.constData(), sizeof(values));
        const std::array<quint16, 8> expected = {0, 1, 2, 2, 3, 0, 65535, 40000};
        QCOMPARE(values, expected);
        QVERIFY(QcIndexPacking::toShortIndices({}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRenderCulling)
#include "test_render_culling.moc"
