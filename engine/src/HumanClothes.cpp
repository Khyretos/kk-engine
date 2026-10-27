#include "kke/HumanClothes.h"

#include "kke/Cloth.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kke {

namespace {

// How a garment is cut. Heights are metres from the waist (tops) or
// fractions of the leg (bottoms, socks, shoes), so a cut scales with
// the Height slider the same way the body does.
enum class Sleeve : uint8_t { None, Cap, Short, Elbow, Long };
enum class Neck : uint8_t { Crew, V, DeepV, Scoop, Turtle, Bare, Halter };
struct Cut {
    enum Kind : uint8_t { Top, Bottom, Skirt, Dress, Coat, Lower, Gloves } kind = Top;
    Sleeve sleeve = Sleeve::Short;
    Neck neck = Neck::Crew;
    float hem = -0.03f;    // Top, Dress, Coat: the bodice's hem, m over the waist (+ up, - down)
    float reach = 0.0f;    // Bottom: down the leg to this fraction over the sole (0.05 ankles, 0.62 mid-thigh);
                           // Skirt, Dress, Coat: of the long skirt (0.3 tennis .. 1 ankles);
                           // Lower (socks, shoes): up the leg to this fraction over the sole;
                           // Gloves: m up the arm from the wrist (0 = the hand only)
    float offset = 0.003f; // m over the body or the layer under it
    bool fingerless = false, hands = true;
};
struct Entry {
    GarmentInfo info;
    Cut cut;
};

const glm::vec3 kWhite{ 0.92f, 0.92f, 0.9f }, kBlack{ 0.08f, 0.08f, 0.09f }, kSky{ 0.35f, 0.65f, 1.0f }, kNight{ 0.16f, 0.18f, 0.24f },
    kDenim{ 0.16f, 0.25f, 0.45f }, kSand{ 0.72f, 0.66f, 0.5f }, kWine{ 0.45f, 0.1f, 0.16f }, kOlive{ 0.3f, 0.36f, 0.2f },
    kCream{ 0.9f, 0.84f, 0.7f }, kRose{ 0.9f, 0.55f, 0.62f }, kGrey{ 0.45f, 0.45f, 0.47f }, kBrown{ 0.36f, 0.2f, 0.11f },
    kCoral{ 0.95f, 0.45f, 0.35f }, kTeal{ 0.12f, 0.45f, 0.45f }, kLilac{ 0.7f, 0.6f, 0.85f };

Entry top(const char* id, const char* label, Sleeve s, Neck n, float hem, const char* fabric = "cotton", glm::vec3 colour = kSky,
          float offset = 0.003f) {
    return { { id, label, "top", fabric, colour }, { Cut::Top, s, n, hem, 0.0f, offset } };
}
Entry outer(const char* id, const char* label, Sleeve s, Neck n, float hem, const char* fabric, glm::vec3 colour, float offset = 0.009f) {
    return { { id, label, "outer", fabric, colour }, { Cut::Top, s, n, hem, 0.0f, offset } };
}
Entry coat(const char* id, const char* label, Sleeve s, Neck n, float tails, const char* fabric, glm::vec3 colour, float offset = 0.009f) {
    return { { id, label, "outer", fabric, colour }, { Cut::Coat, s, n, -0.03f, tails, offset } };
}
Entry bottom(const char* id, const char* label, float reach, const char* fabric = "cotton", glm::vec3 colour = kNight, float offset = 0.002f) {
    return { { id, label, "bottom", fabric, colour }, { Cut::Bottom, Sleeve::None, Neck::Crew, 0.0f, reach, offset } };
}
Entry skirt(const char* id, const char* label, float length, const char* fabric = "cotton", glm::vec3 colour = kNight) {
    return { { id, label, "bottom", fabric, colour }, { Cut::Skirt, Sleeve::None, Neck::Crew, 0.0f, length, 0.004f } };
}
Entry dress(const char* id, const char* label, Sleeve s, Neck n, float length, const char* fabric = "cotton", glm::vec3 colour = kRose) {
    return { { id, label, "dress", fabric, colour }, { Cut::Dress, s, n, -0.03f, length, 0.003f } };
}
Entry socks(const char* id, const char* label, float reach, const char* fabric = "cotton", glm::vec3 colour = kWhite) {
    return { { id, label, "socks", fabric, colour }, { Cut::Lower, Sleeve::None, Neck::Crew, 0.0f, reach, 0.001f } };
}
Entry shoes(const char* id, const char* label, float reach, const char* fabric = "leather", glm::vec3 colour = kBlack, float offset = 0.006f) {
    return { { id, label, "shoes", fabric, colour }, { Cut::Lower, Sleeve::None, Neck::Crew, 0.0f, reach, offset } };
}
Entry gloves(const char* id, const char* label, float reach, bool hands, bool fingerless, const char* fabric = "leather",
             glm::vec3 colour = kBlack) {
    Entry e{ { id, label, "gloves", fabric, colour }, { Cut::Gloves, Sleeve::None, Neck::Crew, 0.0f, reach, reach > 0.0f ? 0.0045f : 0.0015f } };
    e.cut.fingerless = fingerless;
    e.cut.hands = hands;
    return e;
}

const std::vector<Entry>& table() {
    using S = Sleeve;
    using N = Neck;
    static const std::vector<Entry> list = {
        // Tops: sleeve, neckline, hem over the waist.
        top("t-shirt", "T-shirt", S::Short, N::Crew, -0.03f),
        top("v-neck tee", "V-neck tee", S::Short, N::V, -0.03f),
        top("scoop tee", "Scoop-neck tee", S::Short, N::Scoop, -0.03f, "cotton", kCoral),
        top("deep-v tee", "Deep V tee", S::Short, N::DeepV, -0.03f, "cotton", kWine),
        top("three-quarter tee", "Three-quarter tee", S::Elbow, N::Crew, -0.03f, "cotton", kGrey),
        top("cap-sleeve tee", "Cap-sleeve tee", S::Cap, N::Scoop, -0.03f, "cotton", kLilac),
        top("muscle tee", "Muscle tee", S::Cap, N::Crew, -0.06f, "cotton", kGrey),
        top("tank top", "Tank top", S::None, N::Crew, -0.03f),
        top("crop top", "Crop top", S::Short, N::Crew, 0.11f),
        top("crop tank", "Crop tank", S::None, N::Scoop, 0.11f, "cotton", kCoral),
        top("long sleeve", "Long sleeve", S::Long, N::Crew, -0.03f),
        top("polo", "Polo", S::Short, N::V, -0.08f, "cotton", kTeal),
        top("henley", "Henley", S::Long, N::V, -0.08f, "cotton", kSand),
        top("turtleneck", "Turtleneck", S::Long, N::Turtle, -0.03f, "wool", kBlack),
        top("sweater", "Sweater", S::Long, N::Crew, -0.1f, "wool", kWine, 0.006f),
        top("cropped sweater", "Cropped sweater", S::Long, N::Crew, 0.08f, "wool", kCream, 0.006f),
        top("sweatshirt", "Sweatshirt", S::Long, N::Crew, -0.1f, "fleece", kGrey, 0.006f),
        top("hoodie", "Hoodie", S::Long, N::Turtle, -0.12f, "fleece", kNight, 0.008f),
        top("cropped hoodie", "Cropped hoodie", S::Long, N::Turtle, 0.06f, "fleece", kRose, 0.008f),
        top("blouse", "Blouse", S::Elbow, N::V, -0.05f, "satin", kCream),
        top("wrap top", "Wrap top", S::Elbow, N::DeepV, -0.12f, "satin", kWine, 0.005f),
        top("camisole", "Camisole", S::None, N::Scoop, -0.03f, "satin", kRose),
        top("tube top", "Tube top", S::None, N::Bare, 0.1f),
        top("bandeau", "Bandeau", S::None, N::Bare, 0.14f, "cotton", kBlack),
        top("off-shoulder top", "Off-shoulder top", S::Long, N::Bare, -0.03f, "linen", kCream),
        top("halter top", "Halter top", S::None, N::Halter, 0.08f, "cotton", kCoral),
        top("tunic", "Tunic", S::Elbow, N::Scoop, -0.25f, "linen", kSand),
        top("sports bra", "Sports bra", S::None, N::Scoop, 0.14f, "cotton", kBlack, 0.0015f),
        top("rash guard", "Rash guard", S::Long, N::Crew, -0.03f, "rubber", kTeal, 0.0015f),
        top("bodysuit", "Bodysuit", S::Long, N::Scoop, -0.15f, "rubber", kBlack, 0.0015f),
        top("corset", "Corset", S::None, N::Bare, 0.02f, "leather", kWine, 0.004f),
        top("linen shirt", "Linen shirt", S::Long, N::V, -0.12f, "linen", kWhite, 0.005f),
        // Outerwear: over a top, further out.
        outer("denim jacket", "Denim jacket", S::Long, N::V, -0.08f, "denim", kDenim),
        outer("leather jacket", "Leather jacket", S::Long, N::V, -0.08f, "leather", kBlack),
        outer("bomber jacket", "Bomber jacket", S::Long, N::Crew, -0.08f, "canvas", kOlive),
        outer("blazer", "Blazer", S::Long, N::DeepV, -0.14f, "wool", kNight),
        outer("cardigan", "Cardigan", S::Long, N::DeepV, -0.14f, "wool", kSand),
        outer("vest", "Vest", S::None, N::DeepV, -0.1f, "wool", kGrey),
        outer("puffer vest", "Puffer vest", S::None, N::Crew, -0.1f, "canvas", kWine, 0.02f),
        outer("puffer jacket", "Puffer jacket", S::Long, N::Crew, -0.12f, "canvas", kBlack, 0.02f),
        outer("windbreaker", "Windbreaker", S::Long, N::Turtle, -0.1f, "canvas", kSky, 0.01f),
        outer("varsity jacket", "Varsity jacket", S::Long, N::Crew, -0.08f, "wool", kWine),
        outer("track jacket", "Track jacket", S::Long, N::Turtle, -0.06f, "rubber", kNight, 0.007f),
        outer("shrug", "Shrug", S::Elbow, N::DeepV, 0.1f, "wool", kLilac),
        coat("long coat", "Long coat", S::Long, N::DeepV, 0.75f, "wool", kNight),
        coat("trench coat", "Trench coat", S::Long, N::DeepV, 0.6f, "canvas", kSand),
        coat("parka", "Parka", S::Long, N::Turtle, 0.4f, "canvas", kOlive, 0.015f),
        coat("duster", "Duster", S::Long, N::V, 1.0f, "linen", kBrown),
        coat("cape", "Cape", S::Cap, N::Crew, 0.6f, "wool", kWine, 0.02f),
        coat("kimono", "Kimono", S::Long, N::DeepV, 0.9f, "satin", kTeal, 0.012f),
        coat("haori", "Haori", S::Elbow, N::DeepV, 0.3f, "satin", kNight, 0.012f),
        coat("lab coat", "Lab coat", S::Long, N::V, 0.5f, "linen", kWhite),
        // Bottoms: how far down the leg.
        bottom("shorts", "Shorts", 0.62f),
        bottom("short shorts", "Short shorts", 0.72f, "denim", kDenim),
        bottom("bermuda shorts", "Bermuda shorts", 0.5f, "canvas", kSand),
        bottom("cycling shorts", "Cycling shorts", 0.62f, "rubber", kBlack, 0.0012f),
        bottom("briefs", "Briefs", 0.8f, "cotton", kBlack, 0.0012f),
        bottom("breeches", "Breeches", 0.45f, "canvas", kBrown),
        bottom("capri", "Capri trousers", 0.3f, "cotton", kSand),
        bottom("trousers", "Trousers", 0.05f),
        bottom("jeans", "Jeans", 0.05f, "denim", kDenim),
        bottom("wide trousers", "Wide trousers", 0.05f, "linen", kCream, 0.008f),
        bottom("cargo trousers", "Cargo trousers", 0.05f, "canvas", kOlive, 0.006f),
        bottom("chinos", "Chinos", 0.05f, "canvas", kSand),
        bottom("joggers", "Joggers", 0.05f, "fleece", kGrey, 0.004f),
        bottom("leggings", "Leggings", 0.02f, "cotton", kBlack, 0.0012f),
        bottom("leather trousers", "Leather trousers", 0.05f, "leather", kBlack),
        // Skirts: cloth, this much of the long skirt.
        skirt("mini skirt", "Mini skirt", 0.22f, "denim", kDenim),
        skirt("tennis skirt", "Tennis skirt", 0.3f),
        skirt("leather skirt", "Leather skirt", 0.35f, "leather", kBlack),
        skirt("denim skirt", "Denim skirt", 0.4f, "denim", kDenim),
        skirt("pleated skirt", "Pleated skirt", 0.45f, "wool", kNight),
        skirt("skirt", "Skirt", 0.55f),
        skirt("tulle skirt", "Tulle skirt", 0.6f, "net", kRose),
        skirt("midi skirt", "Midi skirt", 0.7f, "linen", kSand),
        skirt("satin skirt", "Satin skirt", 0.75f, "satin", kWine),
        skirt("long skirt", "Long skirt", 1.0f),
        skirt("maxi skirt", "Maxi skirt", 1.0f, "silk", kTeal),
        skirt("hakama", "Hakama", 1.0f, "canvas", kNight),
        // Dresses: a bodice from the suit, a skirt of cloth.
        dress("sundress", "Sundress", S::None, N::Scoop, 0.55f),
        dress("tea dress", "Tea dress", S::Short, N::Crew, 0.55f, "cotton", kLilac),
        dress("shirt dress", "Shirt dress", S::Long, N::V, 0.5f, "linen", kSand),
        dress("denim dress", "Denim dress", S::Short, N::V, 0.45f, "denim", kDenim),
        dress("pinafore", "Pinafore", S::None, N::Scoop, 0.5f, "denim", kDenim),
        dress("sweater dress", "Sweater dress", S::Long, N::Turtle, 0.5f, "wool", kGrey),
        dress("slip dress", "Slip dress", S::None, N::V, 0.6f, "satin", kWine),
        dress("wrap dress", "Wrap dress", S::Elbow, N::DeepV, 0.6f, "silk", kTeal),
        dress("cocktail dress", "Cocktail dress", S::Cap, N::Scoop, 0.4f, "satin", kBlack),
        dress("halter dress", "Halter dress", S::None, N::Halter, 1.0f, "silk", kCoral),
        dress("gown", "Gown", S::None, N::Bare, 1.0f, "satin", kWine),
        dress("ball gown", "Ball gown", S::Cap, N::Scoop, 1.0f, "satin", kCream),
        // Socks: up the leg to.
        socks("ankle socks", "Ankle socks", 0.1f),
        socks("socks", "Socks", 0.2f),
        socks("knee socks", "Knee socks", 0.44f),
        socks("thigh-high socks", "Thigh-high socks", 0.72f, "cotton", kBlack),
        socks("stockings", "Stockings", 0.8f, "net", kBlack),
        socks("tights", "Tights", 1.0f, "cotton", kBlack),
        // Shoes: up the foot or the leg to.
        shoes("sandals", "Sandals", 0.045f, "leather", kBrown),
        shoes("loafers", "Loafers", 0.06f),
        shoes("heels", "Heels", 0.08f, "leather", kBlack, 0.007f),
        shoes("shoes", "Shoes", 0.09f),
        shoes("sneakers", "Sneakers", 0.09f, "canvas", kWhite),
        shoes("high-top sneakers", "High-top sneakers", 0.13f, "canvas", kWine),
        shoes("ankle boots", "Ankle boots", 0.16f, "leather", kBrown),
        shoes("boots", "Boots", 0.3f),
        shoes("wellies", "Wellies", 0.4f, "rubber", kOlive),
        shoes("knee boots", "Knee boots", 0.46f),
        shoes("thigh boots", "Thigh boots", 0.7f),
        // Gloves: from the body's own hands, up the arm.
        gloves("gloves", "Gloves", 0.0f, true, false),
        gloves("fingerless gloves", "Fingerless gloves", 0.0f, true, true, "wool", kGrey),
        gloves("long gloves", "Long gloves", 0.2f, true, false, "satin", kBlack),
        gloves("opera gloves", "Opera gloves", 0.35f, true, false, "satin", kWhite),
        gloves("arm warmers", "Arm warmers", 0.22f, false, false, "wool", kGrey),
    };
    return list;
}

const Entry* findEntry(const std::string& id) {
    for (const Entry& e : table())
        if (e.info.id == id) return &e;
    return nullptr;
}

// Every vertex of a group, from its faces.
std::vector<uint32_t> groupVerts(const HumanPack& pack, const std::string& name) {
    const int g = pack.findGroup(name);
    if (g < 0) return {};
    std::set<uint32_t> out;
    const HumanGroup& group = pack.groups()[static_cast<size_t>(g)];
    for (uint32_t f = group.firstFace; f < group.firstFace + group.faceCount; ++f)
        for (uint32_t v : pack.quads()[f].verts) out.insert(v);
    return { out.begin(), out.end() };
}

// The bone that moves each vertex most ("" = none).
std::vector<std::string> mainBone(const HumanPack& pack) {
    std::vector<std::string> bone(pack.positions().size());
    std::vector<float> best(pack.positions().size(), 0.0f);
    for (const auto& [name, w] : pack.weights())
        for (size_t i = 0; i < w.verts.size(); ++i)
            if (w.weights[i] > best[w.verts[i]]) {
                best[w.verts[i]] = w.weights[i];
                bone[w.verts[i]] = name;
            }
    return bone;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// The body's landmarks the cuts are made from.
struct Anatomy {
    const std::vector<glm::vec3>& shaped;
    std::vector<std::string> bone;
    // The vertices the cuts choose from: the body suit, the long skirt, the
    // skin (gloves). A body that is not a MakeHuman one (GarmentBody) lends
    // its whole skin as the suit.
    std::vector<uint32_t> suit, skirt, skin;
    // Optional, per vertex (a body whose arms are not held down): how far
    // along its arm bone it is (0 the joint, 1 the next), and its distance
    // from the wrist. Sleeves and gloves go by these when they are there,
    // by height when not.
    std::vector<float> armT, armDist;
    float sole = 1e9f, waist = -1e9f, wristTop = -1e9f;
    float neckY = 0.0f, neckZ = 0.0f; // where the neck meets the shoulders (the base of the neck bone)

    explicit Anatomy(const GarmentBody& b)
        : shaped(b.positions), bone(b.bone), suit(b.skin), skin(b.skin), armT(b.armT), armDist(b.armDist) {
        sole = b.sole;
        waist = b.waist;
        wristTop = b.wristTop;
        neckY = b.neckY;
        neckZ = b.neckZ;
    }

    Anatomy(const HumanPack& pack, const std::vector<glm::vec3>& s) : shaped(s), bone(mainBone(pack)) {
        suit = groupVerts(pack, "helper-tights");
        skirt = groupVerts(pack, "helper-skirt");
        skin = groupVerts(pack, "body");
        if (auto it = pack.joints().find("neck01____head"); it != pack.joints().end() && !it->second.empty()) {
            for (uint32_t v : it->second) {
                neckY += shaped[v].y;
                neckZ += shaped[v].z;
            }
            neckY /= static_cast<float>(it->second.size());
            neckZ /= static_cast<float>(it->second.size());
        }
        // The waist is the skirt's top: the bones alone can't say it
        // (spine05 carries the belly and the groin).
        for (uint32_t v : skirt) waist = std::max(waist, shaped[v].y);
        for (uint32_t v : suit) {
            if (foot(v)) sole = std::min(sole, shaped[v].y);
            if (startsWith(bone[v], "wrist")) wristTop = std::max(wristTop, shaped[v].y);
        }
    }
    bool torso(uint32_t v) const {
        const std::string& b = bone[v];
        return startsWith(b, "spine0") || startsWith(b, "breast") || startsWith(b, "clavicle");
    }
    bool shoulder(uint32_t v) const { return startsWith(bone[v], "shoulder01"); }
    bool neck(uint32_t v) const { return startsWith(bone[v], "neck"); }
    bool upperArm(uint32_t v) const { return startsWith(bone[v], "upperarm"); }
    bool lowerArm(uint32_t v) const { return startsWith(bone[v], "lowerarm"); }
    bool hand(uint32_t v) const {
        const std::string& b = bone[v];
        return startsWith(b, "wrist") || startsWith(b, "metacarpal") || startsWith(b, "finger");
    }
    bool arm(uint32_t v) const { return upperArm(v) || lowerArm(v) || hand(v); }
    bool foot(uint32_t v) const { return startsWith(bone[v], "foot") || startsWith(bone[v], "toe"); }
    // Below the waist and not an arm or a foot: the legs and the hips.
    bool below(uint32_t v) const { return !arm(v) && !foot(v) && !torso(v) && !shoulder(v) && !neck(v) && shaped[v].y < waist + 0.04f; }
    // The height of an open neckline at ax (m from the middle), front or
    // back: the cloth lies below it. A crew sits just over the base of the
    // neck; scoops and Vs drop in front, V's straight-sided.
    float collarY(Neck n, float ax, bool front) const {
        float width = 0.075f, drop = 0.018f;
        bool straight = false;
        switch (n) {
        case Neck::Scoop: width = 0.115f, drop = 0.07f; break;
        case Neck::V: width = 0.10f, drop = 0.14f, straight = true; break;
        case Neck::DeepV: width = 0.13f, drop = 0.24f, straight = true; break;
        default: break;
        }
        if (!front) drop = n == Neck::Crew ? 0.004f : 0.02f; // a back neckline barely dips
        const float u = std::min(ax / width, 1.0f);
        const float shape = straight && front ? 1.0f - u : 1.0f - u * u;
        return neckY + 0.012f - drop * shape;
    }
    // The cap sleeve's reach: the shoulder end of the upper arm.
    bool capArm(uint32_t v) const {
        if (!armT.empty()) return upperArm(v) && armT[v] < 0.34f;
        return startsWith(bone[v], "upperarm01") && shaped[v].y > waist + 0.33f;
    }
    // Is this arm vertex within `reach` metres of the wrist (a glove's cuff)?
    bool withinReachOfWrist(uint32_t v, float reach) const {
        return armDist.empty() ? shaped[v].y < wristTop + reach : armDist[v] < reach;
    }
    // A fraction of the leg, from the sole (0) to the waist (1), as a height.
    float legY(float reach) const { return sole + reach * (waist - sole); }
};

// A top's cut: its hem, neckline and sleeves on the body suit.
bool wearsTop(const Anatomy& a, const Cut& c, uint32_t v) {
    const glm::vec3& p = a.shaped[v];
    const bool torso = a.torso(v), shoulder = a.shoulder(v), neck = a.neck(v), arm = a.arm(v);
    if (!torso && !shoulder && !neck && !arm) return false;
    if (p.y <= a.waist + c.hem) return false;
    const float chest = a.waist + 0.30f; // the top of the chest, where necklines start
    const float ax = std::abs(p.x);
    // Open necklines are one smooth curve round the base of the neck, not
    // whatever bone each vertex happens to lean on (that tore the collar
    // wherever neck and torso weights alternate).
    const bool collar = c.neck == Neck::Crew || c.neck == Neck::V || c.neck == Neck::DeepV || c.neck == Neck::Scoop;
    if (collar && a.neckY > 0.0f) {
        if (p.y > a.collarY(c.neck, ax, p.z > a.neckZ) && !arm) return false;
        if (neck) return true;
    } else if (neck) {
        if (c.neck == Neck::Turtle) return true;
        if (c.neck == Neck::Halter) return ax < 0.035f && p.z > 0.0f; // the strap, up the front
        return false;
    }
    switch (c.neck) {
    case Neck::Crew:
    case Neck::Turtle:
    case Neck::V:
    case Neck::DeepV:
    case Neck::Scoop: break;
    case Neck::Bare: // off the shoulders: nothing above the chest, all round
        if (p.y > chest) return false;
        break;
    case Neck::Halter:
        if (p.y > chest + 0.01f && !(ax < 0.035f && p.z > 0.0f)) return false;
        break;
    }
    if (shoulder) return c.sleeve != Sleeve::None;
    if (arm) {
        switch (c.sleeve) {
        case Sleeve::None: return false;
        case Sleeve::Cap: return a.capArm(v);
        case Sleeve::Short: return startsWith(a.bone[v], "upperarm01");
        case Sleeve::Elbow: return a.upperArm(v) || startsWith(a.bone[v], "lowerarm01");
        case Sleeve::Long: return true;
        }
    }
    return true;
}

GarmentShape suitPiece(const Anatomy& a, float offset, const std::function<bool(uint32_t)>& wears) {
    GarmentShape out;
    out.group = "helper-tights";
    out.offset = offset;
    for (uint32_t v : a.suit)
        if (wears(v)) out.verts.push_back(v);
    return out;
}

// A skirt: the long skirt's top part, as cloth pinned at the waist.
GarmentShape skirtPiece(const Anatomy& a, float length, float offset) {
    GarmentShape out;
    out.group = "helper-skirt";
    out.cloth = true;
    out.offset = offset;
    const std::vector<uint32_t>& all = a.skirt;
    if (all.empty()) return out;
    float top = -1e9f, bottom = 1e9f;
    for (uint32_t v : all) {
        top = std::max(top, a.shaped[v].y);
        bottom = std::min(bottom, a.shaped[v].y);
    }
    const float hem = top - length * (top - bottom) - 1e-4f;
    for (uint32_t v : all) {
        if (a.shaped[v].y < hem) continue;
        out.verts.push_back(v);
        if (a.shaped[v].y > top - 0.012f) out.pinned.push_back(v); // the waistband
    }
    return out;
}

// Gloves: the body's own hands, and the arm up to `reach` over the wrist.
GarmentShape glovePiece(const Anatomy& a, const Cut& c) {
    GarmentShape out;
    out.group = "body";
    out.offset = c.offset;
    for (uint32_t v : a.skin) {
        const std::string& b = a.bone[v];
        bool on = false;
        if (a.hand(v)) {
            on = c.hands;
            if (c.fingerless && (b.find("-2.") != std::string::npos || b.find("-3.") != std::string::npos)) on = false;
        } else if (c.reach > 0.0f && (a.lowerArm(v) || a.upperArm(v))) {
            on = a.withinReachOfWrist(v, c.reach);
        }
        if (on) out.verts.push_back(v);
    }
    return out;
}

} // namespace

const std::vector<GarmentInfo>& garments() {
    static const std::vector<GarmentInfo> list = [] {
        std::vector<GarmentInfo> out;
        for (const Entry& e : table()) out.push_back(e.info);
        return out;
    }();
    return list;
}

const std::vector<std::string>& garmentSlots() {
    static const std::vector<std::string> slots = { "top", "outer", "bottom", "dress", "socks", "shoes", "gloves" };
    return slots;
}

const GarmentInfo* findGarment(const std::string& id) {
    const Entry* e = findEntry(id);
    return e ? &e->info : nullptr;
}

bool garmentSlotsClash(const std::string& a, const std::string& b) {
    if (a == b) return true;
    const auto underDress = [](const std::string& s) { return s == "top" || s == "bottom"; };
    return (a == "dress" && underDress(b)) || (b == "dress" && underDress(a));
}

namespace {
std::vector<GarmentShape> piecesOn(const Anatomy& a, const Cut& c) {
    const std::vector<glm::vec3>& shaped = a.shaped;
    std::vector<GarmentShape> out;
    switch (c.kind) {
    case Cut::Top: out.push_back(suitPiece(a, c.offset, [&](uint32_t v) { return wearsTop(a, c, v); })); break;
    case Cut::Bottom:
        // Bottoms rise a little over the waist, under the top (which sits further out).
        out.push_back(suitPiece(a, c.offset, [&](uint32_t v) { return a.below(v) && shaped[v].y > a.legY(c.reach); }));
        break;
    case Cut::Skirt: out.push_back(skirtPiece(a, c.reach, c.offset)); break;
    case Cut::Dress:
    case Cut::Coat:
        out.push_back(suitPiece(a, c.offset, [&](uint32_t v) { return wearsTop(a, c, v); }));
        out.push_back(skirtPiece(a, c.reach, c.kind == Cut::Coat ? c.offset : 0.004f));
        break;
    case Cut::Lower: // socks and shoes: the feet, and the legs up to the cuff
        out.push_back(suitPiece(a, c.offset, [&](uint32_t v) { return (a.foot(v) || a.below(v)) && shaped[v].y < a.legY(c.reach); }));
        break;
    case Cut::Gloves: out.push_back(glovePiece(a, c)); break;
    }
    return out;
}
} // namespace

std::vector<GarmentShape> garmentPieces(const HumanPack& pack, const std::vector<glm::vec3>& shaped, const std::string& id) {
    const Entry* e = findEntry(id);
    if (!e || shaped.size() != pack.positions().size()) return {};
    return piecesOn(Anatomy(pack, shaped), e->cut);
}

std::vector<GarmentShape> garmentPiecesOn(const GarmentBody& body, const std::string& id) {
    const Entry* e = findEntry(id);
    if (!e || body.bone.size() != body.positions.size()) return {};
    std::vector<GarmentShape> out = piecesOn(Anatomy(body), e->cut);
    // The cloth pieces (a skirt, coat tails) are cut from MakeHuman's long
    // skirt, which another body does not have: the caller builds those
    // (garmentSkirt()).
    out.erase(std::remove_if(out.begin(), out.end(), [](const GarmentShape& s) { return s.group == "helper-skirt"; }), out.end());
    return out;
}

bool garmentSkirt(const std::string& id, float* reach, float* offset) {
    const Entry* e = findEntry(id);
    if (!e || (e->cut.kind != Cut::Skirt && e->cut.kind != Cut::Dress && e->cut.kind != Cut::Coat)) return false;
    if (reach) *reach = e->cut.reach;
    if (offset) *offset = e->cut.kind == Cut::Coat ? e->cut.offset : 0.004f;
    return true;
}

GarmentShape garmentShape(const HumanPack& pack, const std::vector<glm::vec3>& shaped, const std::string& id) {
    std::vector<GarmentShape> pieces = garmentPieces(pack, shaped, id);
    return pieces.empty() ? GarmentShape{} : std::move(pieces.front());
}

ModelMesh garmentMesh(const ModelMesh& mesh, const std::vector<uint32_t>& sourceVertex, const GarmentShape& shape,
                      std::vector<uint32_t>* from) {
    ModelMesh out;
    out.skinned = mesh.skinned;
    const std::set<uint32_t> in(shape.verts.begin(), shape.verts.end());
    std::map<uint32_t, uint32_t> index;
    std::vector<uint32_t> source;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
        if (!in.count(sourceVertex[a]) || !in.count(sourceVertex[b]) || !in.count(sourceVertex[c])) continue;
        for (uint32_t v : { a, b, c }) {
            auto [it, added] = index.emplace(v, static_cast<uint32_t>(out.vertices.size()));
            if (added) {
                ModelVertex mv = mesh.vertices[v];
                mv.position += mv.normal * shape.offset;
                out.vertices.push_back(mv);
                source.push_back(v);
            }
            out.indices.push_back(it->second);
        }
    }
    if (from) *from = std::move(source);
    return out;
}

ModelMaterial garmentMaterial(const GarmentInfo& garment, const glm::vec3& colour) {
    const Fabric fabric = clothFabric(garment.fabric);
    ModelMaterial mat;
    mat.name = "Clothes_" + garment.id;
    mat.baseColor = colour;
    // Satin and leather shine, wool and fleece don't; a little of the
    // fabric's gloss as metal-free specular is all a PBR material has.
    mat.roughness = std::clamp(fabric.roughness - 0.3f * fabric.specular, 0.2f, 0.95f);
    mat.metallic = 0.0f;
    return mat;
}

std::vector<uint32_t> skinUnder(const ModelMesh& garment, const ModelMesh& skin) {
    std::vector<uint32_t> under(garment.vertices.size(), 0);
    if (skin.vertices.empty()) return under;
    for (size_t i = 0; i < garment.vertices.size(); ++i) {
        float best = 1e30f;
        for (size_t s = 0; s < skin.vertices.size(); ++s) {
            const glm::vec3 d = skin.vertices[s].position - garment.vertices[i].position;
            if (const float dd = glm::dot(d, d); dd < best) {
                best = dd;
                under[i] = static_cast<uint32_t>(s);
            }
        }
    }
    return under;
}

void keepOutside(ModelMesh& garment, const ModelMesh& skin, const std::vector<uint32_t>& under, float gap) {
    if (under.size() != garment.vertices.size()) return;
    for (size_t i = 0; i < under.size(); ++i) {
        if (under[i] >= skin.vertices.size()) continue;
        const ModelVertex& s = skin.vertices[under[i]];
        const glm::vec3 n = glm::normalize(s.normal);
        const float over = glm::dot(garment.vertices[i].position - s.position, n);
        if (over < gap) garment.vertices[i].position += n * (gap - over);
    }
}

void dress(ModelData& model, const Human& human, const std::vector<HumanLook::Worn>& clothes, bool skeleton) {
    if (clothes.empty()) return;
    Human::BakeOptions options;
    options.groups = { "helper-tights", "helper-skirt", "body" }; // the body for gloves
    options.skeleton = skeleton;
    const Human::Bake helpers = human.bake(options); // the same bones as any bake of this human
    const std::vector<glm::vec3> shaped = human.shapedPositions();
    ModelMesh skin;
    for (const ModelMesh& m : model.meshes)
        if (m.name == "body") skin = m;
    for (const HumanLook::Worn& worn : clothes) {
        const GarmentInfo* info = findGarment(worn.id);
        if (!info) continue;
        for (const GarmentShape& shape : garmentPieces(human.pack(), shaped, worn.id)) {
            for (size_t m = 0; m < helpers.model.meshes.size(); ++m) {
                if (helpers.model.meshes[m].name != shape.group) continue;
                ModelMesh mesh = garmentMesh(helpers.model.meshes[m], helpers.sourceVertex[m], shape);
                if (mesh.indices.empty()) break;
                // Gloves are the skin itself moved out: keeping them "outside"
                // their own vertices would be a no-op, so only the suit's pieces.
                if (shape.group != "body") keepOutside(mesh, skin, skinUnder(mesh, skin), shape.offset);
                model.materials.push_back(garmentMaterial(*info, worn.colour));
                mesh.name = worn.id;
                mesh.material = static_cast<uint32_t>(model.materials.size() - 1);
                model.meshes.push_back(std::move(mesh));
            }
        }
    }
}

} // namespace kke
