#include <QFile>
#include <QTest>
#include <rhi/qshader.h>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// A shader baked by qt_add_shaders for the game (src/app/CMakeLists.txt).
QShader baked(const QString &name)
{
    QFile file(QStringLiteral(QTCRAFT_QSB_DIR) + u'/' + name);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

bool hasSource(const QShader &shader, QShader::Source source)
{
    const QList<QShaderKey> keys = shader.availableShaders();
    return std::ranges::any_of(keys, [source](QShaderKey key) { return key.source() == source; });
}

} // namespace

// The opaque variant of the block fragment shader (QC_OPAQUE, alpha 1, decision E-332) and the solid one (E-756) are baked
// for every RHI backend the plain one is baked for, and differs from it.
class TestShaderVariants : public QObject
{
    Q_OBJECT

private slots:
    void opaqueBlockShaderExistsForEveryBackend()
    {
        const QShader plain = baked(u"block.frag.qsb"_s);
        const QShader opaque = baked(u"block_opaque.frag.qsb"_s);
        QVERIFY(plain.isValid());
        QVERIFY(opaque.isValid());
        QCOMPARE(opaque.stage(), QShader::FragmentStage);
        QCOMPARE(opaque.availableShaders(), plain.availableShaders());
        for (const QShader::Source source : {QShader::SpirvShader, QShader::GlslShader, QShader::HlslShader, QShader::MslShader}) {
            QVERIFY2(hasSource(opaque, source), qPrintable(QString::number(int(source))));
        }
        QVERIFY(opaque.serialized() != plain.serialized());
        // Same resources: the opaque pipeline uses the bindings of the translucent one.
        QCOMPARE(opaque.description().uniformBlocks().size(), plain.description().uniformBlocks().size());
        QCOMPARE(opaque.description().combinedImageSamplers().size(), plain.description().combinedImageSamplers().size());
    }

    // The solid variant (QC_OPAQUE and QC_SOLID, no discard, decision E-756) is baked for the same
    // backends, differs from both others and uses the same resources (same bindings).
    void solidBlockShaderExistsForEveryBackend()
    {
        const QShader plain = baked(u"block.frag.qsb"_s);
        const QShader opaque = baked(u"block_opaque.frag.qsb"_s);
        const QShader solid = baked(u"block_solid.frag.qsb"_s);
        QVERIFY(solid.isValid());
        QCOMPARE(solid.stage(), QShader::FragmentStage);
        QCOMPARE(solid.availableShaders(), plain.availableShaders());
        for (const QShader::Source source : {QShader::SpirvShader, QShader::GlslShader, QShader::HlslShader, QShader::MslShader}) {
            QVERIFY2(hasSource(solid, source), qPrintable(QString::number(int(source))));
        }
        QVERIFY(solid.serialized() != plain.serialized());
        QVERIFY(solid.serialized() != opaque.serialized());
        QCOMPARE(solid.description().uniformBlocks().size(), plain.description().uniformBlocks().size());
        QCOMPARE(solid.description().combinedImageSamplers().size(), plain.description().combinedImageSamplers().size());
    }
};

QTEST_GUILESS_MAIN(TestShaderVariants)
#include "test_shader_variants.moc"
