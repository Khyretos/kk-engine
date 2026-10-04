#include "kke/net/LocalNetworks.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <utility>

namespace kke::net {

namespace {

uint32_t maskOf(uint8_t prefix) { return prefix == 0 ? 0u : ~0u << (32u - prefix); }

#if !defined(_WIN32)
// Windows reports the prefix length itself (OnLinkPrefixLength).
uint8_t prefixOf(uint32_t mask) {
    uint8_t n = 0;
    while (n < 32 && (mask & (0x80000000u >> n))) ++n;
    return n;
}
#endif

} // namespace

std::vector<LocalNetwork> localNetworks() {
    std::vector<LocalNetwork> out;
#if defined(_WIN32)
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && result == ERROR_BUFFER_OVERFLOW; ++tries) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        // Ethernet and Wi-Fi take broadcasts (OpenVPN's TAP and Hamachi
        // show as Ethernet and do too); WireGuard's and Tailscale's
        // adapters are other types and don't.
        const bool broadcast = a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211;
        char name[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, static_cast<int>(sizeof name) - 1, nullptr, nullptr);
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* in = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            LocalNetwork n;
            n.name = name;
            n.address = ntohl(in->sin_addr.s_addr);
            n.prefix = static_cast<uint8_t>(u->OnLinkPrefixLength > 32 ? 32 : u->OnLinkPrefixLength);
            n.broadcast = broadcast;
            out.push_back(std::move(n));
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (const ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        LocalNetwork n;
        n.name = i->ifa_name ? i->ifa_name : "";
        n.address = ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr);
        n.prefix = i->ifa_netmask ? prefixOf(ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_netmask)->sin_addr.s_addr)) : 32;
        // WireGuard and tun devices are point-to-point (or say nothing): no broadcast.
        n.broadcast = (i->ifa_flags & IFF_BROADCAST) && !(i->ifa_flags & IFF_POINTOPOINT);
        out.push_back(std::move(n));
    }
    freeifaddrs(list);
#endif
    return out;
}

uint32_t directedBroadcast(const LocalNetwork& network) {
    if (network.prefix >= 31) return 0;
    return network.address | ~maskOf(network.prefix);
}

std::vector<uint32_t> sweepAddresses(const LocalNetwork& network) {
    const uint8_t prefix = network.prefix >= 23 && network.prefix <= 30 ? network.prefix : 24;
    const uint32_t mask = maskOf(prefix);
    const uint32_t first = (network.address & mask) + 1;
    const uint32_t last = (network.address | ~mask) - 1;
    std::vector<uint32_t> out;
    out.reserve(last - first + 1);
    for (uint32_t a = first; a <= last; ++a)
        if (a != network.address) out.push_back(a);
    return out;
}

std::string ipv4Text(uint32_t address) {
    return std::to_string(address >> 24) + "." + std::to_string((address >> 16) & 0xFF) + "." + std::to_string((address >> 8) & 0xFF) +
           "." + std::to_string(address & 0xFF);
}

} // namespace kke::net
