#include "common/content/registry/QcSignalLinks.h"

#include "common/content/registry/QcContentRegistry.h"

using namespace Qt::StringLiterals;

namespace {

QString headProblem(const QcContentRegistry &content, const QcBlockDefinition &piston)
{
    const std::optional<QcBlockId> head = content.blockId(piston.signal.head);
    if (!head) {
        return u"its head '%1' does not exist"_s.arg(piston.signal.head);
    }

    const QcBlockDefinition &definition = content.block(*head);
    const QcBlockProperty *facing = definition.property(u"facing"_s);
    const QcBlockProperty *sticky = definition.property(u"sticky"_s);
    const QcBlockProperty *pistonFacing = piston.property(u"facing"_s);
    if (definition.signal.kind != QcSignalKind::PistonHead || !facing || !sticky || !pistonFacing) {
        return u"its head '%1' must be a piston_head with 'facing' and 'sticky'"_s.arg(piston.signal.head);
    }
    for (const QString &value : pistonFacing->values) {
        if (!facing->values.contains(value)) {
            return u"its head '%1' has no facing '%2'"_s.arg(piston.signal.head, value);
        }
    }
    if (!sticky->values.contains(u"false"_s) || !sticky->values.contains(u"true"_s)) {
        return u"its head '%1' needs 'sticky' false and true"_s.arg(piston.signal.head);
    }
    return {};
}

QString counterpartProblem(const QcContentRegistry &content, const QcBlockDefinition &block)
{
    const std::optional<QcBlockId> other = content.blockId(block.signal.counterpart);
    if (!other) {
        return u"its counterpart '%1' does not exist"_s.arg(block.signal.counterpart);
    }

    const QcBlockDefinition &definition = content.block(*other);
    if (definition.signal.kind != block.signal.kind || definition.signal.active == block.signal.active) {
        return u"its counterpart '%1' must be the same kind with the opposite 'active'"_s.arg(block.signal.counterpart);
    }
    if (definition.properties != block.properties) {
        return u"its counterpart '%1' must have the same states"_s.arg(block.signal.counterpart);
    }
    return {};
}

} // namespace

namespace QcSignalLinks {

QList<std::pair<QString, QString>> problems(const QcContentRegistry &content)
{
    QList<std::pair<QString, QString>> found;
    for (const QcBlockDefinition &block : content.blocks()) {
        QString problem;
        if (block.signal.kind == QcSignalKind::Piston) {
            problem = headProblem(content, block);
        } else if (block.signal.kind == QcSignalKind::Torch || block.signal.kind == QcSignalKind::Lamp) {
            problem = counterpartProblem(content, block);
        }
        if (!problem.isEmpty()) {
            found.append({block.name, problem});
        }
    }
    return found;
}

} // namespace QcSignalLinks
