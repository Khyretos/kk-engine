#pragma once

// People: the other kind of body a player can pick in the start menu
// (Body: Bean or a person). A person is a Synty POLYGON City character,
// animated with the Universal Animation Library's clips retargeted onto
// it, drawn by kke::ModelModule where the bean would be. The physics is
// the same capsule either way, so nobody is faster or bigger for their
// look.
//
// Without the pack (or the animation library) there are no people: the
// menu offers only Bean, and a person picked on another machine is drawn
// as a bean here.

#include "kke/Animator.h"
#include "kke/ModelAsset.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class Application;
}

namespace party {

struct Bean;

// A bean's person body: its model instance and animator.
struct PersonBody {
    kke::ModelModule::InstanceId instance = 0;
    int body = 0;                            // BeanLook::body it was made for
    std::unique_ptr<kke::Animator> anim;
    int state = -1;                          // what it's playing (People::State)
    bool wasGrounded = true;
    float airTime = 0.0f;
};

class People {
public:
    // Every person there can be, in a fixed order (BeanLook::body - 1), the
    // same on every machine whatever packs it has.
    static const std::vector<std::string>& all();       // "Jock", "Tourist", ...
    static const char* assetOf(int body);                // "SK_Character_Jock" (body >= 1)

    void init(kke::Application& app);
    bool any() const { return m_available.size() > 1; } // anyone besides the bean
    // The bodies this machine can draw, for the menu: 0 (Bean) first.
    const std::vector<int>& available() const { return m_available; }
    bool has(int body) const;

    // Keeps b's person up to date with its look: spawns, swaps or removes
    // the model. Returns false when b is drawn as a bean.
    bool sync(Bean& b);
    void remove(Bean& b);
    // Poses and places it: `m` is the bean's body matrix at its feet
    // (turned, leaning, tumbling), `speed` its ground speed.
    void animate(Bean& b, const glm::mat4& m, float speed, bool cheer, bool visible, float dt);

private:
    struct Rig {
        bool tried = false;
        kke::ModelModule::ModelId model = 0;
        kke::ModelData data;                     // bones + retargeted clips
        std::unique_ptr<kke::AnimationSet> set;
        float yaw = 180.0f;                      // degrees: its forward onto -Z
        float scale = 1.0f;                      // to the capsule's height
    };
    Rig* rig(int body);

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    std::string m_ualFile, m_packDir;
    kke::ModelData m_ual;
    bool m_ualLoaded = false;
    std::vector<std::string> m_paths;         // per body - 1: the FBX ("" when missing)
    std::vector<kke::ModelLoadOptions> m_options;
    std::vector<Rig> m_rigs;
    std::vector<int> m_available;
};

} // namespace party
