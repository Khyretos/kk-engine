#include "kke/SaveSlots.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace kke {

namespace {

// The file: "KKSAVE", format, then the header, the data and a CRC-32 of
// everything before it. Little-endian. A slot's "info." record is the same
// file with no data (its length 0, dataBytes still the real size), so the
// menu lists slots without reading every save.
constexpr char kMagic[6] = { 'K', 'K', 'S', 'A', 'V', 'E' };
constexpr uint16_t kFormat = 1;

template <typename T> void put(std::string& out, T v) {
    for (size_t i = 0; i < sizeof(T); ++i) out.push_back(static_cast<char>((static_cast<uint64_t>(v) >> (8 * i)) & 0xFF));
}

struct Reader {
    const std::string& s;
    size_t at = 0;
    bool ok = true;
    template <typename T> T get() {
        if (!ok || s.size() - at < sizeof(T)) {
            ok = false;
            return T{};
        }
        uint64_t v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(s[at + i])) << (8 * i);
        at += sizeof(T);
        return static_cast<T>(v);
    }
    std::string bytes(uint64_t n) {
        if (!ok || s.size() - at < n) {
            ok = false;
            return {};
        }
        std::string r = s.substr(at, size_t(n));
        at += size_t(n);
        return r;
    }
};

std::string fold(const std::string& game) {
    std::string c = "saves.";
    for (char ch : game) {
        const char l = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
        const bool ok = (l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_' || l == '-' || l == '.';
        if (c.size() < storage::Store::kMaxCollection) c += ok ? l : '_';
    }
    if (c.size() == 6) c += "game";
    return c;
}

int64_t wallClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

SaveSlots::SaveSlots(storage::Store& store, const std::string& game) : m_store(store), m_collection(fold(game)) {}

uint32_t SaveSlots::crc32(const void* data, size_t size) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool SaveSlots::validSlot(const std::string& slot) {
    if (slot.empty() || slot.size() > kMaxSlotName) return false;
    if (slot.front() == ' ' || slot.back() == ' ') return false;
    return std::all_of(slot.begin(), slot.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '_' || ch == '-' || ch == '.';
    });
}

std::string SaveSlots::encode(const SaveInfo& info, const std::string& data) {
    std::string out(kMagic, sizeof(kMagic));
    put<uint16_t>(out, kFormat);
    put<uint32_t>(out, info.version);
    put<int64_t>(out, info.savedAtMs);
    uint64_t play = 0;
    std::memcpy(&play, &info.playSeconds, sizeof(play));
    put<uint64_t>(out, play);
    put<uint16_t>(out, static_cast<uint16_t>(info.slot.size()));
    out += info.slot;
    put<uint16_t>(out, static_cast<uint16_t>(info.title.size()));
    out += info.title;
    put<uint64_t>(out, info.dataBytes);
    put<uint64_t>(out, data.size());
    out += data;
    put<uint32_t>(out, crc32(out.data(), out.size()));
    return out;
}

std::optional<SaveFile> SaveSlots::decode(const std::string& file, std::string* error) {
    auto bad = [&](const char* why) -> std::optional<SaveFile> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (file.size() < sizeof(kMagic) + 4 || std::memcmp(file.data(), kMagic, sizeof(kMagic)) != 0) return bad("not a save file");
    Reader r{ file, file.size() - 4 };
    const uint32_t stored = r.get<uint32_t>();
    if (stored != crc32(file.data(), file.size() - 4)) return bad("the save is damaged (its checksum doesn't match)");
    Reader h{ file, sizeof(kMagic) };
    if (h.get<uint16_t>() != kFormat) return bad("the save file's format is newer than this engine");
    SaveFile out;
    out.info.version = h.get<uint32_t>();
    out.info.savedAtMs = h.get<int64_t>();
    const uint64_t play = h.get<uint64_t>();
    std::memcpy(&out.info.playSeconds, &play, sizeof(play));
    out.info.slot = h.bytes(h.get<uint16_t>());
    out.info.title = h.bytes(h.get<uint16_t>());
    out.info.dataBytes = h.get<uint64_t>();
    const uint64_t len = h.get<uint64_t>();
    out.data = h.bytes(len);
    if (!h.ok || h.at != file.size() - 4) return bad("the save is damaged (its parts don't add up)");
    if (!validSlot(out.info.slot) || out.info.title.size() > kMaxTitle || out.info.version == 0 || !std::isfinite(out.info.playSeconds))
        return bad("the save is damaged (bad header)");
    if (len != 0 && len != out.info.dataBytes) return bad("the save is damaged (its size doesn't match)");
    return out;
}

bool SaveSlots::fail(std::string* error, std::string message) const {
    if (error) *error = std::move(message);
    return false;
}

bool SaveSlots::write(const std::string& slot, const std::string& data, const std::string& title, double playSeconds, std::string* error) {
    if (!validSlot(slot)) return fail(error, "a slot name is 1-64 letters, digits, spaces, _ - or .");
    if (data.size() > kMaxData) return fail(error, "the save is " + std::to_string(data.size() / 1024) + " KB; at most " + std::to_string(kMaxData / 1024 / 1024) + " MB");
    SaveInfo info;
    info.slot = slot;
    info.title = title.substr(0, kMaxTitle);
    info.savedAtMs = now ? now() : wallClockMs();
    info.version = m_version;
    info.playSeconds = std::isfinite(playSeconds) && playSeconds > 0.0 ? playSeconds : 0.0;
    info.dataBytes = data.size();
    const std::string file = encode(info, data);
    const std::string header = encode(info, {});
    std::string problem;
    const bool ok = m_store.transaction([&] {
        if (!m_store.get(m_collection, key(slot))) {
            if (!m_store.lastError().empty()) return false;
            if (m_store.list(m_collection, "info.", kMaxSlots + 1).size() >= kMaxSlots) {
                problem = "all " + std::to_string(kMaxSlots) + " save slots are used; remove one first";
                return false;
            }
        }
        return m_store.put(m_collection, key(slot), file) && m_store.put(m_collection, "info." + slot, header);
    });
    if (!ok) return fail(error, problem.empty() ? "couldn't save: " + m_store.lastError() : problem);
    if (onWritten) onWritten(info, file);
    return true;
}

std::optional<SaveFile> SaveSlots::read(const std::string& slot, std::string* error) {
    if (!validSlot(slot)) {
        fail(error, "a slot name is 1-64 letters, digits, spaces, _ - or .");
        return std::nullopt;
    }
    const std::optional<std::string> file = m_store.get(m_collection, key(slot));
    if (!file) {
        fail(error, m_store.lastError().empty() ? "nothing is saved in '" + slot + "'" : "couldn't read the save: " + m_store.lastError());
        return std::nullopt;
    }
    std::optional<SaveFile> save = decode(*file, error);
    if (save && save->info.slot != slot) {
        fail(error, "the save is damaged (it names another slot)");
        return std::nullopt;
    }
    return save;
}

std::vector<SaveInfo> SaveSlots::list() {
    std::vector<SaveInfo> out;
    for (const storage::Store::Item& item : m_store.list(m_collection, "info.", kMaxSlots))
        if (std::optional<SaveFile> s = decode(item.value); s && item.key == "info." + s->info.slot) out.push_back(std::move(s->info));
    std::stable_sort(out.begin(), out.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.savedAtMs > b.savedAtMs; });
    return out;
}

bool SaveSlots::exists(const std::string& slot) { return validSlot(slot) && m_store.get(m_collection, "info." + slot).has_value(); }

bool SaveSlots::remove(const std::string& slot, std::string* error) {
    if (!validSlot(slot)) return fail(error, "a slot name is 1-64 letters, digits, spaces, _ - or .");
    const bool ok = m_store.transaction([&] { return m_store.erase(m_collection, key(slot)) && m_store.erase(m_collection, "info." + slot); });
    if (!ok) return fail(error, "couldn't remove the save: " + m_store.lastError());
    if (onRemoved) onRemoved(slot);
    return true;
}

std::optional<std::string> SaveSlots::fileOf(const std::string& slot) {
    if (!validSlot(slot)) return std::nullopt;
    return m_store.get(m_collection, key(slot));
}

SaveSlots::Import SaveSlots::importFile(const std::string& file, std::string* error) {
    std::optional<SaveFile> in = decode(file, error);
    if (!in || in->data.size() != in->info.dataBytes) {
        if (in) fail(error, "that is a slot's info, not its save");
        return Import::Damaged;
    }
    if (in->data.size() > kMaxData) {
        fail(error, "the save is bigger than " + std::to_string(kMaxData / 1024 / 1024) + " MB");
        return Import::Damaged;
    }
    const std::string slot = in->info.slot;
    bool older = false;
    std::string problem;
    const bool ok = m_store.transaction([&] {
        if (std::optional<std::string> mine = m_store.get(m_collection, "info." + slot)) {
            if (std::optional<SaveFile> m = decode(*mine); m && m->info.savedAtMs >= in->info.savedAtMs) {
                older = true;
                return false;
            }
        } else if (!m_store.lastError().empty()) {
            return false;
        } else if (m_store.list(m_collection, "info.", kMaxSlots + 1).size() >= kMaxSlots) {
            problem = "all " + std::to_string(kMaxSlots) + " save slots are used";
            return false;
        }
        return m_store.put(m_collection, key(slot), file) && m_store.put(m_collection, "info." + slot, encode(in->info, {}));
    });
    if (older) {
        fail(error, "'" + slot + "' here is as new or newer");
        return Import::Older;
    }
    if (!ok) {
        fail(error, problem.empty() ? "couldn't save: " + m_store.lastError() : problem);
        return Import::Failed;
    }
    return Import::Taken;
}

} // namespace kke
