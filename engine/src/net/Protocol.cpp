#include "kke/net/Protocol.h"

namespace kke::net {

std::optional<MessageType> peekType(const uint8_t* data, size_t size) {
    ReadStream r(data, size);
    const uint32_t type = r.readBits(5);
    if (!r.ok() || type == 0 || type >= static_cast<uint32_t>(MessageType::Count)) return std::nullopt;
    return static_cast<MessageType>(type);
}

} // namespace kke::net
