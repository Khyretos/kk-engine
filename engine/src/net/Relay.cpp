#include "kke/net/Relay.h"

#include "kke/PackSeal.h"

#include <monocypher.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace kke::net {

namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; // Crockford base 32
constexpr uint8_t kMagic[4] = { 'K', 'K', 'E', 'r' };
constexpr uint8_t kVersion = 1;
constexpr double kRegisterEvery = 10.0;   // the relay forgets a server after 30 s of silence
constexpr double kSendEvery = 0.25;       // lookups and punches: a few, a quarter second apart
constexpr size_t kMaxHost = 64, kMaxGame = 32;

bool splitHostPort(const std::string& s, uint16_t defaultPort, std::string& host, uint16_t& port) {
    host = s;
    port = defaultPort;
    const size_t colon = s.rfind(':');
    if (colon != std::string::npos) {
        const std::string p = s.substr(colon + 1);
        if (p.empty() || p.size() > 5 || !std::all_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
        const unsigned long v = std::stoul(p);
        if (v == 0 || v > 65535) return false;
        host = s.substr(0, colon);
        port = static_cast<uint16_t>(v);
    }
    return !host.empty() && host.size() <= kMaxHost;
}

// ---- byte writer / reader: magic, version, type, then fields

struct Writer {
    std::vector<uint8_t> out;
    explicit Writer(relay::Type t) {
        out.assign(kMagic, kMagic + 4);
        out.push_back(kVersion);
        out.push_back(static_cast<uint8_t>(t));
    }
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }
    template <size_t N> void raw(const std::array<uint8_t, N>& a) { out.insert(out.end(), a.begin(), a.end()); }
    void str(const std::string& s, size_t max) {
        const size_t n = std::min(s.size(), max);
        u8(static_cast<uint8_t>(n));
        out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    }
};

struct Reader {
    const std::vector<uint8_t>& d;
    size_t at = 6;
    bool ok;
    Reader(const std::vector<uint8_t>& data, relay::Type t) : d(data), ok(relay::typeOf(data) == t) {}
    uint8_t u8() {
        if (at + 1 > d.size()) { ok = false; return 0; }
        return d[at++];
    }
    uint16_t u16() {
        const uint16_t lo = u8();
        return static_cast<uint16_t>(lo | (u8() << 8));
    }
    template <size_t N> void raw(std::array<uint8_t, N>& a) {
        if (at + N > d.size()) { ok = false; return; }
        std::memcpy(a.data(), d.data() + at, N);
        at += N;
    }
    std::string str(size_t max) {
        const size_t n = u8();
        if (n > max || at + n > d.size()) { ok = false; return {}; }
        std::string s(d.begin() + static_cast<std::ptrdiff_t>(at), d.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        return s;
    }
    // Every field read and nothing after it (a Lookup's padding aside).
    bool end(bool padded = false) const { return ok && (padded || at == d.size()); }
};

bool printable(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x20 && c != 0x7F; });
}

bool validCode(const std::string& c) {
    return c.size() == kJoinCodeLength && std::all_of(c.begin(), c.end(), [](char ch) { return std::strchr(kAlphabet, ch) && ch != 0; });
}

} // namespace

// ------------------------------------------------------------------ codes

std::string newJoinCode() {
    uint8_t r[kJoinCodeLength];
    if (!seal::randomBytes(r, sizeof r)) return {};
    std::string code;
    for (uint8_t b : r) code += kAlphabet[b & 31];
    return code;
}

std::optional<std::string> normalizeJoinCode(const std::string& typed) {
    std::string code;
    for (char c : typed) {
        if (c == '-' || c == ' ' || c == '\t') continue;
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        if (!std::strchr(kAlphabet, c) || c == 0) return std::nullopt;
        code += c;
    }
    if (code.size() != kJoinCodeLength) return std::nullopt;
    return code;
}

std::string formatJoinCode(const std::string& code) {
    if (code.size() != kJoinCodeLength) return code;
    return code.substr(0, 3) + "-" + code.substr(3);
}

std::optional<JoinTarget> parseJoinTarget(const std::string& text, const std::string& defaultRelay, std::string* error) {
    const size_t at = text.find('@');
    const auto code = normalizeJoinCode(text.substr(0, at));
    if (!code) {
        if (error) *error = "'" + text.substr(0, at) + "' isn't a join code (six letters and digits, like K7M-Q2P)";
        return std::nullopt;
    }
    JoinTarget t;
    t.code = *code;
    const std::string relayText = at == std::string::npos ? defaultRelay : text.substr(at + 1);
    if (relayText.empty()) {
        if (error) *error = "no relay to ask for that code: type it as CODE@relay-address, or set the game's relay (KKE_NET_RELAY)";
        return std::nullopt;
    }
    if (!splitHostPort(relayText, kDefaultRelayPort, t.relayHost, t.relayPort)) {
        if (error) *error = "'" + relayText + "' isn't a relay address (host or host:port)";
        return std::nullopt;
    }
    return t;
}

bool looksLikeJoinCode(const std::string& text) { return normalizeJoinCode(text.substr(0, text.find('@'))).has_value(); }

// ------------------------------------------------------------------ datagrams

namespace relay {

bool isRelayDatagram(const uint8_t* data, size_t size) { return size >= 6 && std::memcmp(data, kMagic, 4) == 0 && data[4] == kVersion; }

std::optional<Type> typeOf(const std::vector<uint8_t>& d) {
    if (!isRelayDatagram(d.data(), d.size()) || d[5] < uint8_t(Type::Register) || d[5] > uint8_t(Type::Bye)) return std::nullopt;
    return static_cast<Type>(d[5]);
}

std::vector<uint8_t> encode(const Register& m) {
    Writer w(Type::Register);
    w.str(m.game, kMaxGame);
    w.raw(m.secret);
    w.str(m.code, kJoinCodeLength);
    w.raw(m.serverKey);
    return w.out;
}
std::vector<uint8_t> encode(const Registered& m) {
    Writer w(Type::Registered);
    w.str(m.code, kJoinCodeLength);
    w.str(m.publicHost, kMaxHost);
    w.u16(m.publicPort);
    return w.out;
}
std::vector<uint8_t> encode(const Lookup& m) {
    Writer w(Type::Lookup);
    w.str(m.code, kJoinCodeLength);
    w.str(m.game, kMaxGame);
    w.out.resize(kLookupSize, 0);
    return w.out;
}
std::vector<uint8_t> encode(const Found& m) {
    Writer w(Type::Found);
    w.raw(m.token);
    w.str(m.serverHost, kMaxHost);
    w.u16(m.serverPort);
    w.raw(m.serverKey);
    return w.out;
}
std::vector<uint8_t> encode(const Refused& m) {
    Writer w(Type::Refused);
    w.str(m.reason, 120);
    return w.out;
}
std::vector<uint8_t> encode(const Introduce& m) {
    Writer w(Type::Introduce);
    w.raw(m.token);
    w.str(m.clientHost, kMaxHost);
    w.u16(m.clientPort);
    w.u16(m.relayPort);
    return w.out;
}
std::vector<uint8_t> encode(const Punch& m) {
    Writer w(Type::Punch);
    w.raw(m.token);
    w.u8(m.answer ? 1 : 0);
    return w.out;
}
std::vector<uint8_t> encode(const Open& m) {
    Writer w(Type::Open);
    w.raw(m.token);
    return w.out;
}
std::vector<uint8_t> encode(const Bye& m) {
    Writer w(Type::Bye);
    w.raw(m.secret);
    return w.out;
}

std::optional<Register> decodeRegister(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Register);
    Register m;
    m.game = r.str(kMaxGame);
    r.raw(m.secret);
    m.code = r.str(kJoinCodeLength);
    r.raw(m.serverKey);
    if (!r.end() || m.game.empty() || !printable(m.game) || (!m.code.empty() && !validCode(m.code))) return std::nullopt;
    return m;
}
std::optional<Registered> decodeRegistered(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Registered);
    Registered m;
    m.code = r.str(kJoinCodeLength);
    m.publicHost = r.str(kMaxHost);
    m.publicPort = r.u16();
    if (!r.end() || !validCode(m.code) || !printable(m.publicHost)) return std::nullopt;
    return m;
}
std::optional<Lookup> decodeLookup(const std::vector<uint8_t>& d) {
    if (d.size() < kLookupSize) return std::nullopt; // unpadded: could amplify
    Reader r(d, Type::Lookup);
    Lookup m;
    m.code = r.str(kJoinCodeLength);
    m.game = r.str(kMaxGame);
    if (!r.end(true) || !validCode(m.code) || m.game.empty() || !printable(m.game)) return std::nullopt;
    return m;
}
std::optional<Found> decodeFound(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Found);
    Found m;
    r.raw(m.token);
    m.serverHost = r.str(kMaxHost);
    m.serverPort = r.u16();
    r.raw(m.serverKey);
    if (!r.end() || m.serverHost.empty() || !printable(m.serverHost) || m.serverPort == 0) return std::nullopt;
    return m;
}
std::optional<Refused> decodeRefused(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Refused);
    Refused m;
    m.reason = r.str(120);
    if (!r.end() || !printable(m.reason)) return std::nullopt;
    return m;
}
std::optional<Introduce> decodeIntroduce(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Introduce);
    Introduce m;
    r.raw(m.token);
    m.clientHost = r.str(kMaxHost);
    m.clientPort = r.u16();
    m.relayPort = r.u16();
    if (!r.end() || m.clientHost.empty() || !printable(m.clientHost) || m.clientPort == 0 || m.relayPort == 0) return std::nullopt;
    return m;
}
std::optional<Punch> decodePunch(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Punch);
    Punch m;
    r.raw(m.token);
    const uint8_t a = r.u8();
    if (!r.end() || a > 1) return std::nullopt;
    m.answer = a == 1;
    return m;
}
std::optional<Open> decodeOpen(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Open);
    Open m;
    r.raw(m.token);
    if (!r.end()) return std::nullopt;
    return m;
}
std::optional<Bye> decodeBye(const std::vector<uint8_t>& d) {
    Reader r(d, Type::Bye);
    Bye m;
    r.raw(m.secret);
    if (!r.end()) return std::nullopt;
    return m;
}

} // namespace relay

// ------------------------------------------------------------------ RelayHost

bool RelayHost::start(const std::vector<std::string>& relays, const std::string& game, const relay::Key& serverKey, const Saved* saved,
                      std::string* error) {
    m_relays.clear();
    for (const std::string& r : relays) {
        Relay rel;
        if (!splitHostPort(r, kDefaultRelayPort, rel.name, rel.port)) {
            if (error) *error = "relay '" + r + "' isn't host or host:port";
            return false;
        }
        rel.host = m_socket.resolve(rel.name);
        if (rel.host.empty()) {
            if (error) *error = "relay '" + rel.name + "': no such host (a typo, or no internet?)";
            return false;
        }
        m_relays.push_back(rel);
    }
    m_game = game;
    m_key = serverKey;
    m_code.clear();
    m_confirmed = false;
    if (saved && validCode(saved->code)) {
        m_code = saved->code; // asked for again; shown once a relay confirms it
        m_secret = saved->secret;
    } else if (!seal::randomBytes(m_secret.data(), m_secret.size(), error)) {
        return false;
    }
    m_nextRegister = 0;
    m_warnedSilent = false;
    return true;
}

void RelayHost::stop() {
    if (m_relays.empty()) return;
    for (const Relay& r : m_relays) m_socket.sendRaw(r.host, r.port, relay::encode(relay::Bye{ m_secret }));
    m_relays.clear();
    m_code.clear();
    m_confirmed = false;
}

void RelayHost::update(double now) {
    m_now = now;
    if (m_relays.empty() || now < m_nextRegister) return;
    m_nextRegister = now + kRegisterEvery;
    relay::Register reg;
    reg.game = m_game;
    reg.secret = m_secret;
    reg.code = m_code;
    reg.serverKey = m_key;
    for (Relay& r : m_relays) {
        m_socket.sendRaw(r.host, r.port, relay::encode(reg));
        if (!m_warnedSilent && r.lastAnswer < 0 && now > 3 * kRegisterEvery && log) {
            log("relay " + r.name + ":" + std::to_string(r.port) + " doesn't answer (is it running, with UDP " + std::to_string(r.port) + " open?)");
            m_warnedSilent = true;
        }
    }
    // Forget relayed players' addresses after a while; a new Introduce brings them back.
    std::erase_if(m_relayed, [now](const Relayed& r) { return now - r.since > 3600.0; });
}

void RelayHost::onDatagram(const std::string& host, uint16_t port, const std::vector<uint8_t>& data) {
    const auto relayIt = std::find_if(m_relays.begin(), m_relays.end(), [&](const Relay& r) { return r.port == port && r.host == host; });
    const auto type = relay::typeOf(data);
    if (!type) return;
    if (*type == relay::Type::Punch) {
        // A player's punch reached us: our router lets them in. Answer, so they know.
        if (const auto p = relay::decodePunch(data); p && !p->answer) m_socket.sendRaw(host, port, relay::encode(relay::Punch{ p->token, true }));
        return;
    }
    // Everything else only from a relay we registered with.
    if (relayIt == m_relays.end()) return;
    Relay* rel = &*relayIt;
    if (*type == relay::Type::Registered) {
        const auto m = relay::decodeRegistered(data);
        if (!m) return;
        rel->lastAnswer = m_now;
        if (m_confirmed && m->code == m_code) return;
        const bool changed = m_confirmed; // a second relay, or a relay that restarted and gave another code
        m_code = m->code;
        m_confirmed = true;
        if (log)
            log(std::string(changed ? "join code is now " : "join code ") + joinText() + " (players type it in Multiplayer; the relay sees this server at " +
                m->publicHost + ":" + std::to_string(m->publicPort) + ")");
        if (onCode) onCode(m_code);
        return;
    }
    if (*type == relay::Type::Refused) {
        if (const auto m = relay::decodeRefused(data); m && log) log("relay " + rel->name + ": " + m->reason);
        return;
    }
    if (*type == relay::Type::Introduce) {
        const auto m = relay::decodeIntroduce(data);
        if (!m) return;
        rel->lastAnswer = m_now;
        // Toward the player (a direct path, if both routers allow it) and
        // toward the relay's port for them (the relayed path).
        for (int i = 0; i < 3; ++i) {
            m_socket.sendRaw(m->clientHost, m->clientPort, relay::encode(relay::Punch{ m->token, false }));
            m_socket.sendRaw(host, m->relayPort, relay::encode(relay::Open{ m->token }));
        }
        std::erase_if(m_relayed, [&](const Relayed& r) { return r.relayHost == host && r.relayPort == m->relayPort; });
        m_relayed.push_back({ host, m->relayPort, m->clientHost, m_now });
        if (m_relayed.size() > 256) m_relayed.erase(m_relayed.begin());
    }
}

std::string RelayHost::joinText() const {
    if (!m_confirmed) return {};
    std::string s = formatJoinCode(m_code);
    if (!m_relays.empty())
        s += "@" + m_relays.front().name + (m_relays.front().port == kDefaultRelayPort ? "" : ":" + std::to_string(m_relays.front().port));
    return s;
}

std::string RelayHost::realAddress(const std::string& host, uint16_t port) const {
    for (const Relayed& r : m_relayed)
        if (r.relayHost == host && r.relayPort == port) return r.clientHost;
    return {};
}

// ------------------------------------------------------------------ RelayJoin

bool RelayJoin::start(const JoinTarget& target, const std::string& game, double now, std::string* error) {
    m_target = target;
    m_target.relayHost = m_socket.resolve(target.relayHost);
    m_game = game;
    if (m_target.relayHost.empty()) {
        fail("relay '" + target.relayHost + "': no such host (a typo, or no internet?)");
        if (error) *error = m_error;
        return false;
    }
    m_error.clear();
    m_state = State::LookingUp;
    m_status = "asking relay " + target.relayHost + " for " + formatJoinCode(target.code);
    m_started = now;
    if (!m_socket.sendRaw(m_target.relayHost, m_target.relayPort, relay::encode(relay::Lookup{ target.code, game }))) {
        fail("can't reach relay '" + target.relayHost + "' (a typo, or no internet?)");
        if (error) *error = m_error;
        return false;
    }
    m_nextSend = now + kSendEvery * 4;
    return true;
}

void RelayJoin::fail(const std::string& why) {
    m_state = State::Failed;
    m_error = why;
    m_status = why;
}

void RelayJoin::update(double now) {
    if (m_state == State::LookingUp) {
        if (now - m_started > lookupTimeout) return fail("relay " + m_target.relayHost + " didn't answer (is it running? is UDP " + std::to_string(m_target.relayPort) + " open?)");
        if (now >= m_nextSend) {
            m_nextSend = now + kSendEvery * 4;
            m_socket.sendRaw(m_target.relayHost, m_target.relayPort, relay::encode(relay::Lookup{ m_target.code, m_game }));
        }
        return;
    }
    if (m_state != State::Punching) return;
    if (m_punchStarted < 0) m_punchStarted = m_nextSend = now;
    if (now - m_punchStarted >= punchTime) {
        // No answer to our punches: through the relay, then.
        m_state = State::Relayed;
        m_status = "connecting through the relay";
        m_connectHost = m_target.relayHost;
        m_connectPort = m_target.relayPort;
        return;
    }
    if (now >= m_nextSend) {
        m_nextSend = now + kSendEvery;
        m_socket.sendRaw(m_found.serverHost, m_found.serverPort, relay::encode(relay::Punch{ m_found.token, false }));
    }
}

void RelayJoin::onDatagram(const std::string& host, uint16_t port, const std::vector<uint8_t>& data) {
    const auto type = relay::typeOf(data);
    if (!type) return;
    if (m_state == State::LookingUp && host == m_target.relayHost && port == m_target.relayPort) {
        if (*type == relay::Type::Refused) {
            if (const auto m = relay::decodeRefused(data)) fail("relay: " + m->reason);
            return;
        }
        if (*type != relay::Type::Found) return;
        const auto m = relay::decodeFound(data);
        if (!m) return;
        m_found = *m;
        if (forceRelay) {
            m_state = State::Relayed;
            m_status = "connecting through the relay";
            m_connectHost = m_target.relayHost;
            m_connectPort = m_target.relayPort;
            return;
        }
        m_state = State::Punching;
        m_status = "trying a direct connection";
        m_punchStarted = -1; // the clock starts at the next update
        return;
    }
    if (m_state != State::Punching || *type != relay::Type::Punch) return;
    const auto p = relay::decodePunch(data);
    if (!p || crypto_verify16(p->token.data(), m_found.token.data()) != 0) return;
    if (host != m_found.serverHost || port != m_found.serverPort) return;
    // The server's punch (or its answer to ours) got through: talk directly.
    if (!p->answer) m_socket.sendRaw(host, port, relay::encode(relay::Punch{ p->token, true }));
    m_state = State::Direct;
    m_status = "connecting directly";
    m_connectHost = host;
    m_connectPort = port;
}

} // namespace kke::net
