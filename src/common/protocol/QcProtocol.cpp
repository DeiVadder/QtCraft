#include "common/protocol/QcProtocol.h"

#include "common/protocol/QcProtocolCodec.h"

#include <QDataStream>
#include <QIODevice>

using namespace Qt::StringLiterals;

namespace {

constexpr QDataStream::Version streamVersion = QDataStream::Qt_6_8;

template<std::size_t... I>
[[nodiscard]] QcResult<QcMessage> decodeByTag(quint16 tag, QDataStream &stream, std::index_sequence<I...>)
{
    QcResult<QcMessage> result = qcError(u"Unknown message type %1"_s.arg(tag));
    const auto tryAlternative = [&]<std::size_t Index>() {
        if (tag != Index) {
            return false;
        }
        result = QcProtocolCodec::decode(QcProtocolCodec::Tag<std::variant_alternative_t<Index, QcMessage>>{}, stream);
        return true;
    };
    (void)(tryAlternative.template operator()<I>() || ...);
    return result;
}

} // namespace

namespace QcProtocol {

QByteArray encode(const QcMessage &message)
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setVersion(streamVersion);
    stream << static_cast<quint16>(message.index());
    std::visit([&stream](const auto &payload) { QcProtocolCodec::encode(stream, payload); }, message);
    return frame;
}

quint32 predictionSequence(const QcMessage &message)
{
    if (const auto *finish = std::get_if<QcMsg::FinishMining>(&message)) {
        return finish->sequence;
    }
    if (const auto *use = std::get_if<QcMsg::UseItem>(&message)) {
        return use->sequence;
    }
    return 0;
}

bool isSentBy(std::size_t messageIndex, Peer sender)
{
    const auto is = [messageIndex]<typename T>() {
        return messageIndex == QcMessage(T{}).index();
    };
    if (is.template operator()<QcMsg::Disconnect>() || is.template operator()<QcMsg::CloseMachine>()
        || is.template operator()<QcMsg::CloseTrade>() || is.template operator()<QcMsg::CloseStonecutter>()
        || is.template operator()<QcMsg::CloseEnchanting>() || is.template operator()<QcMsg::CloseAnvil>()) {
        return true;
    }

    const bool clientMessage = is.template operator()<QcMsg::Hello>() || is.template operator()<QcMsg::PlayerInput>()
                               || is.template operator()<QcMsg::StartMining>() || is.template operator()<QcMsg::CancelMining>()
                               || is.template operator()<QcMsg::FinishMining>() || is.template operator()<QcMsg::UseItem>()
                               || is.template operator()<QcMsg::SelectHotbarSlot>()
                               || is.template operator()<QcMsg::MoveInventoryItem>() || is.template operator()<QcMsg::ChatInput>()
                               || is.template operator()<QcMsg::AttackEntity>() || is.template operator()<QcMsg::Respawn>()
                               || is.template operator()<QcMsg::UseEntity>() || is.template operator()<QcMsg::QuickMoveItem>()
                               || is.template operator()<QcMsg::TakeCraftingResult>() || is.template operator()<QcMsg::CraftRecipe>()
                               || is.template operator()<QcMsg::CloseCrafting>() || is.template operator()<QcMsg::ConsumeItem>()
                               || is.template operator()<QcMsg::DropItem>() || is.template operator()<QcMsg::SetAppearance>()
                               || is.template operator()<QcMsg::DebugSubscribe>() || is.template operator()<QcMsg::LoginResponse>()
                               || is.template operator()<QcMsg::CreativeItem>() || is.template operator()<QcMsg::PickBlock>()
                               || is.template operator()<QcMsg::Trade>() || is.template operator()<QcMsg::DrawLauncher>()
                               || is.template operator()<QcMsg::ThrowItem>() || is.template operator()<QcMsg::EditSign>()
                               || is.template operator()<QcMsg::SelectStonecutterRecipe>() || is.template operator()<QcMsg::Pong>()
                               || is.template operator()<QcMsg::AdminSubscribe>() || is.template operator()<QcMsg::AdminRequest>()
                               || is.template operator()<QcMsg::ChooseEnchantment>() || is.template operator()<QcMsg::RenameAnvil>()
                               || is.template operator()<QcMsg::ForgeAnvil>() || is.template operator()<QcMsg::PauseRequest>()
                               || is.template operator()<QcMsg::LatencyProbe>();
    return clientMessage == (sender == Peer::Client);
}

[[nodiscard]] QcResult<QcMessage> decode(const QByteArray &frame, Peer sender)
{
    if (frame.size() > maxFrameBytes) {
        return qcError(u"Frame too large"_s);
    }

    QDataStream stream(frame);
    stream.setVersion(streamVersion);
    quint16 tag = 0;
    stream >> tag;
    if (stream.status() != QDataStream::Ok) {
        return qcError(u"Empty frame"_s);
    }

    if (tag >= std::variant_size_v<QcMessage>) {
        return qcError(u"Unknown message type %1"_s.arg(tag));
    }
    if (!isSentBy(tag, sender)) {
        return qcError(u"Message type %1 is not allowed from this peer"_s.arg(tag));
    }

    QcResult<QcMessage> message = decodeByTag(tag, stream, std::make_index_sequence<std::variant_size_v<QcMessage>>());
    if (message && stream.status() != QDataStream::Ok) {
        return qcError(u"Truncated message of type %1"_s.arg(tag));
    }
    if (message && !stream.atEnd()) {
        return qcError(u"Trailing bytes in message of type %1"_s.arg(tag));
    }
    return message;
}

} // namespace QcProtocol
