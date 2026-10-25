#include "kke/Human.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace kke {

namespace {

// The body-type variables, and the levels MakeHuman's macro targets are
// made at. A level sits at a slider value; a target's weight for one
// variable is 1 at its level and falls off linearly to 0 at the levels
// beside it. A level with no target of its own ("averageheight") still
// counts as a place where the others reach 0.
enum Var { Gender, Age, Muscle, Weight, Height, Proportions, Cup, Firmness, African, Asian, Caucasian, VarCount };

struct Level {
    const char* word;
    Var var;
    int level;
};

constexpr Level kLevels[] = {
    { "female", Gender, 0 },          { "male", Gender, 1 },
    { "baby", Age, 0 },               { "child", Age, 1 },           { "young", Age, 2 },          { "old", Age, 3 },
    { "minmuscle", Muscle, 0 },       { "averagemuscle", Muscle, 1 }, { "maxmuscle", Muscle, 2 },
    { "minweight", Weight, 0 },       { "averageweight", Weight, 1 }, { "maxweight", Weight, 2 },
    { "minheight", Height, 0 },       { "maxheight", Height, 2 },
    { "uncommonproportions", Proportions, 0 }, { "idealproportions", Proportions, 2 },
    { "mincup", Cup, 0 },             { "averagecup", Cup, 1 },      { "maxcup", Cup, 2 },
    { "minfirmness", Firmness, 0 },   { "averagefirmness", Firmness, 1 }, { "maxfirmness", Firmness, 2 },
    { "african", African, 0 },        { "asian", Asian, 0 },         { "caucasian", Caucasian, 0 },
};

// Where each variable's levels sit on its slider. Age: 1 year, 11, 25
// and 90 (MakeHuman's own ages for its baby, child, young and old bodies).
const std::vector<float>& levelValues(Var v) {
    static const std::vector<float> two{ 0.0f, 1.0f }, three{ 0.0f, 0.5f, 1.0f }, age{ 0.0f, 0.1875f, 0.5f, 1.0f };
    switch (v) {
    case Gender: return two;
    case Age: return age;
    default: return three;
    }
}

float hat(const std::vector<float>& at, int level, float x) {
    const float p = at[level];
    if (x <= p) {
        if (level == 0) return 1.0f;
        const float lo = at[level - 1];
        return x <= lo ? 0.0f : (x - lo) / (p - lo);
    }
    if (level + 1 >= static_cast<int>(at.size())) return 1.0f;
    const float hi = at[level + 1];
    return x >= hi ? 0.0f : (hi - x) / (hi - p);
}

Var macroVar(const std::string& macro) {
    static const std::pair<const char*, Var> names[] = {
        { "Gender", Gender }, { "Age", Age }, { "Muscle", Muscle }, { "Weight", Weight }, { "Height", Height },
        { "BodyProportions", Proportions }, { "BreastSize", Cup }, { "BreastFirmness", Firmness },
        { "African", African }, { "Asian", Asian }, { "Caucasian", Caucasian },
    };
    for (auto [n, v] : names)
        if (macro == n) return v;
    return VarCount;
}

bool isAncestry(Var v) { return v == African || v == Asian || v == Caucasian; }

// Area-weighted normals per pack vertex over the given quads.
std::vector<glm::vec3> vertexNormals(const HumanPack& pack, const std::vector<glm::vec3>& p, const std::vector<uint32_t>& quads) {
    std::vector<glm::vec3> n(p.size(), glm::vec3(0.0f));
    for (uint32_t qi : quads) {
        const auto& v = pack.quads()[qi].verts;
        // A quad's normal from its diagonals (twice its area, for a flat one).
        const glm::vec3 fn = glm::cross(p[v[2]] - p[v[0]], p[v[3]] - p[v[1]]);
        for (int k = 0; k < 4; ++k)
            if (k < 3 || v[3] != v[2]) n[v[k]] += fn;
    }
    for (glm::vec3& x : n) {
        const float l = glm::length(x);
        x = l > 1e-12f ? x / l : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return n;
}

} // namespace

Human::Human(const HumanPack& pack) : m_pack(&pack) {
    for (size_t t = 0; t < pack.targets().size(); ++t) {
        const std::string& name = pack.targets()[t].name;
        const std::string base = name.substr(name.rfind('/') + 1);
        MacroTarget m;
        m.target = static_cast<int>(t);
        bool all = true;
        std::stringstream words(base);
        for (std::string w; std::getline(words, w, '-');) {
            if (w == "universal") continue;
            const Level* found = nullptr;
            for (const Level& l : kLevels)
                if (w == l.word) found = &l;
            if (!found) {
                all = false;
                break;
            }
            m.words.emplace_back(found->var, found->level);
        }
        if (all && !m.words.empty()) m_macroTargets.push_back(std::move(m));
    }
}

void Human::set(const std::string& modifier, float value) {
    const int i = m_pack->findModifier(modifier);
    if (i < 0) {
        m_values[modifier] = value;
        return;
    }
    const HumanModifier& mod = m_pack->modifiers()[i];
    value = std::clamp(value, mod.min, mod.max);
    const Var var = macroVar(mod.macro);
    if (!isAncestry(var)) {
        m_values[modifier] = value;
        return;
    }
    // The three ancestry values always add up to 1: the other two share
    // what is left in the proportion they had.
    std::vector<std::string> others;
    for (const HumanModifier& m : m_pack->modifiers())
        if (isAncestry(macroVar(m.macro)) && m.name != modifier) others.push_back(m.name);
    float rest = 0.0f;
    for (const std::string& o : others) rest += get(o);
    for (const std::string& o : others)
        m_values[o] = rest > 1e-6f ? get(o) / rest * (1.0f - value) : (1.0f - value) / static_cast<float>(others.size());
    m_values[modifier] = value;
}

float Human::get(const std::string& modifier) const {
    if (auto it = m_values.find(modifier); it != m_values.end()) return it->second;
    const int i = m_pack->findModifier(modifier);
    return i < 0 ? 0.0f : m_pack->modifiers()[i].defaultValue;
}

void Human::reset() { m_values.clear(); }

std::map<std::string, float> Human::values() const {
    std::map<std::string, float> out;
    for (const auto& [name, v] : m_values) {
        const int i = m_pack->findModifier(name);
        if (i < 0 || std::abs(v - m_pack->modifiers()[i].defaultValue) > 1e-6f) out[name] = v;
    }
    return out;
}

float Human::macroWeight(const MacroTarget& m, const float* value) const {
    float w = 1.0f;
    for (auto [var, level] : m.words) {
        const Var v = static_cast<Var>(var);
        w *= isAncestry(v) ? value[v] : hat(levelValues(v), level, value[v]);
        if (w <= 0.0f) return 0.0f;
    }
    return w;
}

std::vector<std::pair<int, float>> Human::targetWeights() const {
    std::vector<std::pair<int, float>> out;
    std::array<float, VarCount> value{};
    for (const HumanModifier& mod : m_pack->modifiers())
        if (const Var v = macroVar(mod.macro); v != VarCount) value[v] = get(mod.name);
    for (const MacroTarget& m : m_macroTargets)
        if (const float w = macroWeight(m, value.data()); w > 1e-5f) out.emplace_back(m.target, w);
    for (const HumanModifier& mod : m_pack->modifiers()) {
        if (!mod.macro.empty() || mod.targets.empty()) continue;
        const float v = get(mod.name);
        if (mod.targets.size() == 1) {
            if (v > 1e-5f) out.emplace_back(mod.targets[0], v);
        } else if (v < -1e-5f) {
            out.emplace_back(mod.targets[0], -v);
        } else if (v > 1e-5f) {
            out.emplace_back(mod.targets[1], v);
        }
    }
    return out;
}

std::vector<glm::vec3> Human::shapedPositions() const {
    std::vector<glm::vec3> p = m_pack->positions();
    for (auto [t, w] : targetWeights()) {
        const HumanTarget& tg = m_pack->targets()[t];
        for (size_t k = 0; k < tg.verts.size(); ++k) p[tg.verts[k]] += tg.offsets[k] * w;
    }
    return p;
}

glm::vec3 Human::jointPosition(const std::string& joint, const std::vector<glm::vec3>& shaped) const {
    auto it = m_pack->joints().find(joint);
    if (it == m_pack->joints().end() || it->second.empty()) return glm::vec3(0.0f);
    glm::vec3 sum(0.0f);
    for (uint32_t v : it->second) sum += shaped[v];
    return sum / static_cast<float>(it->second.size());
}

void Human::placeSkeleton(ModelData& model, const std::vector<glm::vec3>& shaped) const {
    const auto& defs = m_pack->bones();
    std::vector<glm::mat4> world(defs.size());
    for (size_t b = 0; b < defs.size(); ++b) {
        const HumanBoneDef& d = defs[b];
        const glm::vec3 head = jointPosition(d.head, shaped), tail = jointPosition(d.tail, shaped);
        glm::vec3 y = tail - head;
        y = glm::length(y) > 1e-8f ? glm::normalize(y) : glm::vec3(0.0f, 1.0f, 0.0f);
        // Roll: the bone's x axis is the normal of its plane of three joints
        // (MakeHuman's rotation planes), made square to the bone.
        glm::vec3 x(0.0f);
        if (auto pl = m_pack->planes().find(d.plane); pl != m_pack->planes().end()) {
            const glm::vec3 a = jointPosition(pl->second[0], shaped), b2 = jointPosition(pl->second[1], shaped),
                            c = jointPosition(pl->second[2], shaped);
            x = glm::cross(b2 - a, c - a);
        }
        x -= y * glm::dot(x, y);
        if (glm::length(x) < 1e-8f) {
            x = glm::cross(y, std::abs(y.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f));
        }
        x = glm::normalize(x);
        const glm::vec3 z = glm::cross(x, y);
        world[b] = glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(head, 1.0f));
        ModelBone& bone = model.bones[b];
        bone.localRest = d.parent >= 0 ? glm::inverse(world[d.parent]) * world[b] : world[b];
        bone.inverseBind = glm::inverse(world[b]);
    }
}

Human::Bake Human::bake(const BakeOptions& options) const {
    const HumanPack& pack = *m_pack;
    Bake out;
    ModelData& model = out.model;
    model.sourcePath = "makehuman";
    const std::vector<glm::vec3> shaped = shapedPositions();

    // Skin weights per pack vertex: the four strongest bones, renormalised.
    std::vector<std::vector<std::pair<float, uint32_t>>> influence(shaped.size());
    if (options.skeleton) {
        model.bones.resize(pack.bones().size());
        for (size_t b = 0; b < pack.bones().size(); ++b) {
            model.bones[b].name = pack.bones()[b].name;
            model.bones[b].parent = pack.bones()[b].parent;
            auto w = pack.weights().find(pack.bones()[b].name);
            if (w == pack.weights().end()) continue;
            for (size_t k = 0; k < w->second.verts.size(); ++k)
                influence[w->second.verts[k]].emplace_back(w->second.weights[k], static_cast<uint32_t>(b));
        }
        placeSkeleton(model, shaped);
    }

    struct Look {
        const char* prefix;
        glm::vec3 colour;
        float roughness;
    };
    static const Look looks[] = {
        { "body", { 0.80f, 0.62f, 0.52f }, 0.6f }, { "helper-l-eye", { 0.92f, 0.92f, 0.90f }, 0.15f },
        { "helper-r-eye", { 0.92f, 0.92f, 0.90f }, 0.15f }, { "helper-upper-teeth", { 0.95f, 0.93f, 0.86f }, 0.3f },
        { "helper-lower-teeth", { 0.95f, 0.93f, 0.86f }, 0.3f }, { "helper-tongue", { 0.75f, 0.35f, 0.38f }, 0.5f },
    };

    for (const std::string& groupName : options.groups) {
        const int g = pack.findGroup(groupName);
        if (g < 0) continue;
        std::vector<uint32_t> quads;
        for (uint32_t q = 0; q < pack.quads().size(); ++q)
            if (pack.quads()[q].group == g) quads.push_back(q);
        if (quads.empty()) continue;
        const std::vector<glm::vec3> normals = vertexNormals(pack, shaped, quads);

        ModelMaterial mat;
        mat.name = "Human_" + groupName;
        for (const Look& l : looks)
            if (groupName == l.prefix) {
                mat.baseColor = l.colour;
                mat.roughness = l.roughness;
            }
        model.materials.push_back(mat);

        ModelMesh mesh;
        mesh.name = groupName;
        mesh.material = static_cast<uint32_t>(model.materials.size() - 1);
        mesh.skinned = options.skeleton;
        std::vector<uint32_t> source;
        std::unordered_map<uint64_t, uint32_t> corner; // (pack vertex, uv) -> model vertex
        auto vertexFor = [&](uint32_t v, uint32_t uv) {
            const uint64_t key = (uint64_t(v) << 32) | uv;
            if (auto it = corner.find(key); it != corner.end()) return it->second;
            ModelVertex mv;
            mv.position = shaped[v];
            mv.normal = normals[v];
            if (uv < pack.uvs().size()) mv.uv = glm::vec2(pack.uvs()[uv].x, 1.0f - pack.uvs()[uv].y);
            if (options.skeleton) {
                auto inf = influence[v];
                std::sort(inf.begin(), inf.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                if (inf.size() > 4) inf.resize(4);
                float sum = 0.0f;
                for (const auto& [w, b] : inf) sum += w;
                if (sum <= 0.0f) {
                    mv.joints = glm::uvec4(0u);
                    mv.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                } else {
                    for (size_t k = 0; k < inf.size(); ++k) {
                        mv.joints[static_cast<int>(k)] = inf[k].second;
                        mv.weights[static_cast<int>(k)] = inf[k].first / sum;
                    }
                }
            }
            const uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(mv);
            source.push_back(v);
            corner.emplace(key, index);
            return index;
        };
        for (uint32_t qi : quads) {
            const HumanQuad& q = pack.quads()[qi];
            std::array<uint32_t, 4> c{};
            for (int k = 0; k < 4; ++k) c[k] = vertexFor(q.verts[k], q.uvs[k]);
            mesh.indices.insert(mesh.indices.end(), { c[0], c[1], c[2] });
            if (q.verts[3] != q.verts[2]) mesh.indices.insert(mesh.indices.end(), { c[0], c[2], c[3] });
        }
        model.meshes.push_back(std::move(mesh));
        out.sourceVertex.push_back(std::move(source));
    }
    if (model.meshes.empty()) throw std::runtime_error("human: none of the groups to bake are in the pack");

    model.boundsMin = glm::vec3(1e30f);
    model.boundsMax = glm::vec3(-1e30f);
    for (const ModelMesh& m : model.meshes)
        for (const ModelVertex& v : m.vertices) {
            model.boundsMin = glm::min(model.boundsMin, v.position);
            model.boundsMax = glm::max(model.boundsMax, v.position);
        }
    return out;
}

void Human::reshape(Bake& bake) const {
    const std::vector<glm::vec3> shaped = shapedPositions();
    ModelData& model = bake.model;
    model.boundsMin = glm::vec3(1e30f);
    model.boundsMax = glm::vec3(-1e30f);
    for (size_t m = 0; m < model.meshes.size() && m < bake.sourceVertex.size(); ++m) {
        ModelMesh& mesh = model.meshes[m];
        const int g = m_pack->findGroup(mesh.name);
        std::vector<uint32_t> quads;
        for (uint32_t q = 0; q < m_pack->quads().size(); ++q)
            if (static_cast<int>(m_pack->quads()[q].group) == g) quads.push_back(q);
        const std::vector<glm::vec3> normals = vertexNormals(*m_pack, shaped, quads);
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            const uint32_t v = bake.sourceVertex[m][i];
            mesh.vertices[i].position = shaped[v];
            mesh.vertices[i].normal = normals[v];
            model.boundsMin = glm::min(model.boundsMin, shaped[v]);
            model.boundsMax = glm::max(model.boundsMax, shaped[v]);
        }
    }
    if (!model.bones.empty()) placeSkeleton(model, shaped);
}

} // namespace kke
