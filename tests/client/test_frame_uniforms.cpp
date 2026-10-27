#include "app/rendering/QcRenderPipelines.h"

#include <QFile>
#include <QTest>
#include <rhi/qshader.h>

#include <cstddef>
#include <optional>

using namespace Qt::StringLiterals;

// The Frame uniform block (QcRenderPipelines::FrameUniforms, decision E-883) as the baked shaders of
// the block, entity and hand pipelines reflect it: every member at the offset the C++ struct writes
// it to, and the vertex and fragment stage of a pipeline declaring the identical block (QRhi's GL
// backend turns uniform blocks into struct uniforms that must match across the stages).
namespace {

// A shader baked by qt_add_shaders for the game (src/app/CMakeLists.txt).
QShader baked(const QString &name)
{
    QFile file(QStringLiteral(QTCRAFT_QSB_DIR) + u'/' + name);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

std::optional<QShaderDescription::UniformBlock> frameBlock(const QShader &shader)
{
    const QList<QShaderDescription::UniformBlock> blocks = shader.description().uniformBlocks();
    for (const QShaderDescription::UniformBlock &block : blocks) {
        if (block.blockName == "Frame") {
            return block;
        }
    }
    return std::nullopt;
}

// The member's offset in the block, -1 when it has none of that name.
int offsetOf(const QShaderDescription::UniformBlock &block, const QByteArray &name)
{
    for (const QShaderDescription::BlockVariable &member : block.members) {
        if (member.name == name) {
            return member.offset;
        }
    }
    return -1;
}

bool sameMembers(const QShaderDescription::UniformBlock &a, const QShaderDescription::UniformBlock &b)
{
    if (a.size != b.size || a.binding != b.binding || a.members.size() != b.members.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.members.size(); ++i) {
        const QShaderDescription::BlockVariable &x = a.members.at(i);
        const QShaderDescription::BlockVariable &y = b.members.at(i);
        if (x.name != y.name || x.type != y.type || x.offset != y.offset || x.size != y.size) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestFrameUniforms : public QObject
{
    Q_OBJECT

private slots:
    void membersSitWhereTheStructWritesThem_data()
    {
        QTest::addColumn<QString>("shader");
        for (const QString &name : {u"block.vert.qsb"_s, u"block.frag.qsb"_s, u"block_opaque.frag.qsb"_s, u"block_solid.frag.qsb"_s,
                                    u"entity.vert.qsb"_s, u"entity.frag.qsb"_s, u"hand.vert.qsb"_s}) {
            QTest::newRow(qPrintable(name)) << name;
        }
    }

    void membersSitWhereTheStructWritesThem()
    {
        using QcRenderPipelines::FrameUniforms;
        QFETCH(QString, shader);
        const std::optional<QShaderDescription::UniformBlock> block = frameBlock(baked(shader));
        QVERIFY(block);
        QCOMPARE(block->binding, 0);
        QCOMPARE(block->size, static_cast<int>(sizeof(FrameUniforms)));
        QCOMPARE(offsetOf(*block, "viewProjection"), static_cast<int>(offsetof(FrameUniforms, viewProjection)));
        QCOMPARE(offsetOf(*block, "cameraPosition"), static_cast<int>(offsetof(FrameUniforms, cameraPosition)));
        QCOMPARE(offsetOf(*block, "fogColor"), static_cast<int>(offsetof(FrameUniforms, fogColor)));
        QCOMPARE(offsetOf(*block, "params"), static_cast<int>(offsetof(FrameUniforms, params)));
        QCOMPARE(offsetOf(*block, "extra"), static_cast<int>(offsetof(FrameUniforms, extra)));
        QCOMPARE(offsetOf(*block, "atlas"), static_cast<int>(offsetof(FrameUniforms, atlas)));
        QCOMPARE(offsetOf(*block, "animation"), static_cast<int>(offsetof(FrameUniforms, animation)));
        QCOMPARE(offsetOf(*block, "drawCamera"), static_cast<int>(offsetof(FrameUniforms, drawCamera)));
        QCOMPARE(offsetOf(*block, "lights"), static_cast<int>(offsetof(FrameUniforms, lights)));
    }

    void stagesOfAPipelineDeclareTheSameBlock_data()
    {
        QTest::addColumn<QString>("vertex");
        QTest::addColumn<QString>("fragment");
        // The pairs of QcWorldRendererResources and the character preview.
        QTest::newRow("block") << u"block.vert.qsb"_s << u"block.frag.qsb"_s;
        QTest::newRow("block opaque") << u"block.vert.qsb"_s << u"block_opaque.frag.qsb"_s;
        QTest::newRow("block solid") << u"block.vert.qsb"_s << u"block_solid.frag.qsb"_s;
        QTest::newRow("entity") << u"entity.vert.qsb"_s << u"entity.frag.qsb"_s;
        QTest::newRow("hand") << u"hand.vert.qsb"_s << u"entity.frag.qsb"_s;
    }

    void stagesOfAPipelineDeclareTheSameBlock()
    {
        QFETCH(QString, vertex);
        QFETCH(QString, fragment);
        const std::optional<QShaderDescription::UniformBlock> a = frameBlock(baked(vertex));
        const std::optional<QShaderDescription::UniformBlock> b = frameBlock(baked(fragment));
        QVERIFY(a && b);
        QCOMPARE(a->structName, b->structName);
        QVERIFY(sameMembers(*a, *b));
    }
};

QTEST_GUILESS_MAIN(TestFrameUniforms)
#include "test_frame_uniforms.moc"
