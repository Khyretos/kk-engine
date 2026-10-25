#include "kke/HumanPack.h"

#include "kke/CookedFile.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace kke {

namespace {

constexpr char kMagic[8] = { 'K', 'K', 'H', 'P', 'A', 'C', 'K', '1' };
constexpr uint32_t kVersion = 1;
constexpr float kToMetres = 0.1f;      // MakeHuman works in decimetres
constexpr float kTargetStep = 0.001f;  // cook_human.py's STEP, in decimetres

// Bounds-checked little-endian reads over one buffer.
class Reader {
public:
    Reader(const uint8_t* data, size_t size, const std::string& what) : m_data(data), m_size(size), m_what(what) {}

    size_t pos() const { return m_pos; }
    bool done() const { return m_pos >= m_size; }
    void need(size_t n) const {
        if (n > m_size - m_pos) throw std::runtime_error(m_what + ": cut short");
    }
    void align() { m_pos = std::min(m_size, (m_pos + 3) & ~size_t(3)); }
    void skip(size_t n) {
        need(n);
        m_pos += n;
    }
    const uint8_t* bytes(size_t n) {
        need(n);
        const uint8_t* p = m_data + m_pos;
        m_pos += n;
        return p;
    }
    template <typename T> T get() {
        T v;
        std::memcpy(&v, bytes(sizeof(T)), sizeof(T));
        return v;
    }
    template <typename T> std::vector<T> array(size_t count) {
        align();
        if (count > (m_size - m_pos) / sizeof(T)) throw std::runtime_error(m_what + ": cut short");
        std::vector<T> out(count);
        if (count) std::memcpy(out.data(), bytes(count * sizeof(T)), count * sizeof(T));
        return out;
    }
    // A 16-bit array stored as its low bytes, then its high bytes.
    std::vector<uint16_t> planar16(size_t count) {
        need(count * 2);
        const uint8_t* lo = bytes(count);
        const uint8_t* hi = bytes(count);
        std::vector<uint16_t> out(count);
        for (size_t i = 0; i < count; ++i) out[i] = static_cast<uint16_t>(lo[i] | (hi[i] << 8));
        return out;
    }
    std::string string() {
        align();
        const uint16_t n = get<uint16_t>();
        const uint8_t* p = bytes(n);
        return std::string(reinterpret_cast<const char*>(p), n);
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
    std::string m_what;
};

std::vector<uint8_t> inflate(const uint8_t* data, size_t size, uint32_t rawSize, const std::string& what) {
    std::vector<uint8_t> out(rawSize);
    const int got = stbi_zlib_decode_buffer(reinterpret_cast<char*>(out.data()), static_cast<int>(rawSize),
                                            reinterpret_cast<const char*>(data), static_cast<int>(size));
    if (got != static_cast<int>(rawSize)) throw std::runtime_error(what + ": the morph targets don't unpack");
    return out;
}

std::string jsonString(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

HumanPack HumanPack::load(const std::string& path) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!cooked::readAssetFile(path, bytes, &error)) throw std::runtime_error(path + ": " + error);
    return parse(bytes, path);
}

HumanPack HumanPack::parse(const std::vector<uint8_t>& bytes, const std::string& name) {
    HumanPack pack;
    Reader file(bytes.data(), bytes.size(), name);
    if (bytes.size() < 16 || std::memcmp(file.bytes(8), kMagic, 8) != 0)
        throw std::runtime_error(name + ": not a human pack (tools/makehuman/cook_human.py makes them)");
    if (const uint32_t v = file.get<uint32_t>(); v != kVersion)
        throw std::runtime_error(name + ": human pack version " + std::to_string(v) + ", this engine reads " + std::to_string(kVersion));
    const uint32_t chunks = file.get<uint32_t>();

    nlohmann::json meta;
    bool haveMeta = false, havePositions = false, haveFaces = false, haveTargets = false;
    for (uint32_t c = 0; c < chunks; ++c) {
        file.align();
        const std::string tag(reinterpret_cast<const char*>(file.bytes(4)), 4);
        const uint32_t length = file.get<uint32_t>();
        const uint8_t* payload = file.bytes(length);
        Reader r(payload, length, name + " " + tag);
        if (tag == "META") {
            meta = nlohmann::json::parse(payload, payload + length, nullptr, false);
            if (meta.is_discarded() || !meta.is_object()) throw std::runtime_error(name + ": META is not JSON");
            haveMeta = true;
        } else if (tag == "VPOS") {
            const std::vector<float> f = r.array<float>(length / 12 * 3);
            pack.m_positions.resize(f.size() / 3);
            for (size_t i = 0; i < pack.m_positions.size(); ++i)
                pack.m_positions[i] = glm::vec3(f[3 * i], f[3 * i + 1], f[3 * i + 2]) * kToMetres;
            havePositions = true;
        } else if (tag == "VTUV") {
            const std::vector<float> f = r.array<float>(length / 8 * 2);
            pack.m_uvs.resize(f.size() / 2);
            for (size_t i = 0; i < pack.m_uvs.size(); ++i) pack.m_uvs[i] = glm::vec2(f[2 * i], f[2 * i + 1]);
        } else if (tag == "FACE") {
            const uint32_t n = r.get<uint32_t>();
            const std::vector<uint32_t> v = r.array<uint32_t>(size_t(n) * 4);
            const std::vector<uint32_t> t = r.array<uint32_t>(size_t(n) * 4);
            const std::vector<uint16_t> g = r.array<uint16_t>(n);
            pack.m_quads.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                for (int k = 0; k < 4; ++k) {
                    pack.m_quads[i].verts[k] = v[4 * i + k];
                    pack.m_quads[i].uvs[k] = t[4 * i + k];
                }
                pack.m_quads[i].group = g[i];
            }
            haveFaces = true;
        } else if (tag == "TGTZ") {
            const uint32_t rawSize = r.get<uint32_t>();
            const std::vector<uint8_t> raw = inflate(payload + 4, length - 4, rawSize, name);
            Reader t(raw.data(), raw.size(), name + " targets");
            const uint32_t count = t.get<uint32_t>();
            std::vector<uint32_t> sizes(count);
            pack.m_targets.resize(count);
            size_t total = 0;
            for (uint32_t i = 0; i < count; ++i) {
                pack.m_targets[i].name = t.string();
                t.align();
                sizes[i] = t.get<uint32_t>();
                total += sizes[i];
            }
            t.align();
            const std::vector<uint16_t> steps = t.planar16(total);
            const std::vector<uint16_t> xs = t.planar16(total), ys = t.planar16(total), zs = t.planar16(total);
            const float scale = kTargetStep * kToMetres;
            size_t e = 0;
            for (uint32_t i = 0; i < count; ++i) {
                HumanTarget& tg = pack.m_targets[i];
                tg.verts.resize(sizes[i]);
                tg.offsets.resize(sizes[i]);
                uint32_t vert = 0;
                for (uint32_t k = 0; k < sizes[i]; ++k, ++e) {
                    vert += steps[e];
                    if (vert > 0xFFFF) throw std::runtime_error(name + ": target " + tg.name + " runs past vertex 65535");
                    tg.verts[k] = static_cast<uint16_t>(vert);
                    tg.offsets[k] = glm::vec3(static_cast<int16_t>(xs[e]), static_cast<int16_t>(ys[e]), static_cast<int16_t>(zs[e])) * scale;
                }
                pack.m_targetIndex[tg.name] = static_cast<int>(i);
            }
            haveTargets = true;
        } else if (tag == "WGHT") {
            const uint32_t bones = r.get<uint32_t>();
            for (uint32_t b = 0; b < bones; ++b) {
                const std::string bone = r.string();
                r.align();
                const uint32_t n = r.get<uint32_t>();
                BoneWeights w;
                w.verts = r.array<uint16_t>(n);
                w.weights = r.array<float>(n);
                pack.m_weights[bone] = std::move(w);
            }
        }
        // Unknown chunks are skipped: a newer cooker may add some.
    }
    if (!haveMeta || !havePositions || !haveFaces || !haveTargets)
        throw std::runtime_error(name + ": human pack is missing a part (META, VPOS, FACE or TGTZ)");

    const size_t nverts = pack.m_positions.size();
    for (const HumanQuad& q : pack.m_quads)
        for (int k = 0; k < 4; ++k)
            if (q.verts[k] >= nverts || (!pack.m_uvs.empty() && q.uvs[k] >= pack.m_uvs.size()))
                throw std::runtime_error(name + ": a face points past the mesh");
    for (const HumanTarget& t : pack.m_targets)
        if (!t.verts.empty() && t.verts.back() >= nverts) throw std::runtime_error(name + ": target " + t.name + " points past the mesh");

    if (auto src = meta.find("source"); src != meta.end() && src->is_object())
        for (auto& [k, v] : src->items())
            if (v.is_string()) pack.m_source[k] = v.get<std::string>();
    pack.m_licence = jsonString(meta, "licence");

    if (auto groups = meta.find("groups"); groups != meta.end() && groups->is_array())
        for (const auto& g : *groups)
            pack.m_groups.push_back({ jsonString(g, "name"), g.value("firstFace", 0u), g.value("faceCount", 0u) });

    if (auto mods = meta.find("modifiers"); mods != meta.end() && mods->is_array())
        for (const auto& m : *mods) {
            HumanModifier mod;
            mod.group = jsonString(m, "group");
            mod.name = jsonString(m, "name");
            mod.macro = jsonString(m, "macro");
            mod.min = m.value("min", 0.0f);
            mod.max = m.value("max", 1.0f);
            mod.defaultValue = m.value("default", 0.0f);
            if (auto ts = m.find("targets"); ts != m.end() && ts->is_array())
                for (const auto& t : *ts) {
                    const int i = t.is_string() ? pack.findTarget(t.get<std::string>()) : -1;
                    if (i < 0) throw std::runtime_error(name + ": modifier " + mod.name + " names a missing target");
                    mod.targets.push_back(i);
                }
            if (!mod.macro.empty()) {
                // The body-type sliders all run 0..1 around their middle, except
                // the ancestry mix, which starts as a third each.
                const bool ancestry = mod.macro == "African" || mod.macro == "Asian" || mod.macro == "Caucasian";
                mod.min = 0.0f;
                mod.max = 1.0f;
                mod.defaultValue = ancestry ? 1.0f / 3.0f : 0.5f;
            }
            pack.m_modifierIndex[mod.name] = static_cast<int>(pack.m_modifiers.size());
            pack.m_modifiers.push_back(std::move(mod));
        }
    if (auto d = meta.find("descriptions"); d != meta.end() && d->is_object())
        for (auto& [k, v] : d->items())
            if (v.is_string()) pack.m_descriptions[k] = v.get<std::string>();

    if (auto skel = meta.find("skeleton"); skel != meta.end() && skel->is_object()) {
        std::map<std::string, int> boneIndex;
        if (auto bones = skel->find("bones"); bones != skel->end() && bones->is_array())
            for (const auto& b : *bones) {
                HumanBoneDef def;
                def.name = jsonString(b, "name");
                def.head = jsonString(b, "head");
                def.tail = jsonString(b, "tail");
                def.plane = jsonString(b, "plane");
                const std::string parent = jsonString(b, "parent");
                if (!parent.empty()) {
                    auto it = boneIndex.find(parent);
                    if (it == boneIndex.end()) throw std::runtime_error(name + ": bone " + def.name + " comes before its parent " + parent);
                    def.parent = it->second;
                }
                boneIndex[def.name] = static_cast<int>(pack.m_bones.size());
                pack.m_bones.push_back(std::move(def));
            }
        if (auto joints = skel->find("joints"); joints != skel->end() && joints->is_object())
            for (auto& [k, v] : joints->items()) {
                std::vector<uint32_t> verts;
                for (const auto& i : v)
                    if (i.is_number_unsigned() && i.get<uint32_t>() < nverts) verts.push_back(i.get<uint32_t>());
                pack.m_joints[k] = std::move(verts);
            }
        if (auto planes = skel->find("planes"); planes != skel->end() && planes->is_object())
            for (auto& [k, v] : planes->items())
                if (v.is_array() && v.size() == 3 && v[0].is_string() && v[1].is_string() && v[2].is_string())
                    pack.m_planes[k] = { v[0].get<std::string>(), v[1].get<std::string>(), v[2].get<std::string>() };
    }
    for (auto& [bone, w] : pack.m_weights)
        for (uint16_t v : w.verts)
            if (v >= nverts) throw std::runtime_error(name + ": weights for " + bone + " point past the mesh");
    return pack;
}

int HumanPack::findGroup(const std::string& name) const {
    for (size_t i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].name == name) return static_cast<int>(i);
    return -1;
}

int HumanPack::findTarget(const std::string& name) const {
    auto it = m_targetIndex.find(name);
    return it == m_targetIndex.end() ? -1 : it->second;
}

int HumanPack::findModifier(const std::string& name) const {
    auto it = m_modifierIndex.find(name);
    return it == m_modifierIndex.end() ? -1 : it->second;
}

std::string HumanPack::description(const std::string& modifier) const {
    auto it = m_descriptions.find(modifier);
    return it == m_descriptions.end() ? std::string() : it->second;
}

} // namespace kke
