#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kke::net {

// The IPv4 networks this computer is on, for finding games without any
// setup (docs/NETWORKING.md "Finding games"). A home LAN takes a
// broadcast; a VPN tunnel (WireGuard, Tailscale's /32, ...) carries no
// broadcast at all, so a search asks every address of it instead.
struct LocalNetwork {
    std::string name;       // "wg0", "Ethernet 2"
    uint32_t address = 0;   // this computer's address on it, host byte order
    uint8_t prefix = 0;     // 24 for 255.255.255.0
    bool broadcast = false; // takes broadcasts (Ethernet, Wi-Fi); false: a tunnel
};

// Up, IPv4, not loopback. Empty where the OS can't say.
std::vector<LocalNetwork> localNetworks();

// The subnet's broadcast address (host byte order); 0 for /31 and /32.
uint32_t directedBroadcast(const LocalNetwork& network);

// The addresses a search asks one by one on a tunnel (host byte order,
// never the network's own address). Small subnets (/23 to /30) are asked
// whole; a /31, /32 or anything bigger than a /23 is asked as the /24
// around this computer's address, which is how VPN tools hand them out
// (10.8.0.0/24, 100.64.x.0/24): at most 509 addresses either way.
std::vector<uint32_t> sweepAddresses(const LocalNetwork& network);

std::string ipv4Text(uint32_t address); // host byte order -> "10.8.0.2"

} // namespace kke::net
