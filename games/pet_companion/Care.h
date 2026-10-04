#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace pet_companion {

// Looking after the pet, Tamagotchi and Digimon tamer style (README.md,
// "Care"). The AI core already gives the dog hunger, thirst and
// tiredness (kke::ai::Species::needs) and acts on them: it eats from the
// food bowl, drinks from the water bowl and lies down when it is tired.
// On top of those this keeps what only the game knows:
//   - happiness: play, pats and a good fetch raise it; hunger, thirst,
//     mess in the garden and being left alone lower it;
//   - health: falls while it goes hungry or thirsty for long or lives
//     among its own poop, comes back slowly when it is looked after;
//   - digestion: what it ate comes out again a while later (a poop to
//     clean up), and drinking makes it pee.
// A neglected pet gets sick and sad, mopes and sleeps a lot, and gets
// better once it is cared for again; left without food or water for long
// after that (its health at zero for a minute) it dies, and you can adopt
// a new one (Kees, 2026-10-03).
// Pure logic, no engine: the game feeds it and reads it each frame.
class Care {
public:
    struct Needs {
        float hunger = 0.0f, thirst = 0.0f, tiredness = 0.0f; // 0 fine .. 1 desperate (the AI's)
    };
    struct World {
        int poops = 0;            // lying in the garden
        float oldestPoop = 0.0f;  // seconds the oldest has been there
        float playerDistance = 0.0f;
        bool playing = false;     // fetching, agility, being petted
    };

    void update(const Needs& n, const World& w, float dt);

    // Things that happened.
    void ate(float amount);     // share of a meal (hunger it took away)
    void drank(float amount);
    void petted(float dt);      // while a hand is on its head
    void praised(float amount); // a good fetch, a clean agility run
    void pooped() { m_digestion = 0.0f; }
    void peed() { m_bladder = 0.0f; }

    // What it wants to do about it.
    bool needsToPoop() const { return m_digestion >= 1.0f; }
    bool needsToPee() const { return m_bladder >= 1.0f; }
    bool sick() const { return m_health < 0.3f; }
    // Its health has been at zero for kDyingTime: it has died.
    bool dead() const { return m_dead; }
    // 0 .. 1 of the way from no health left to dying (for the warning).
    float dying() const { return m_dying / kDyingTime; }
    // A new pet: everything back to the start.
    void adopt() { *this = Care(); }
    static constexpr float kDyingTime = 60.0f;

    float happiness() const { return m_happy; }
    float health() const { return m_health; }
    float excitement() const { return m_excited; }
    void excite(float amount);
    Needs needs() const { return m_needs; }

    // One or two words for the HUD ("hungry", "over the moon"), and the
    // HUD colour for it.
    std::string mood() const;
    std::string moodColor() const;

    nlohmann::json save() const;
    void load(const nlohmann::json& j);
    void setHappiness(float v) { m_happy = v; }

private:
    Needs m_needs;
    float m_happy = 0.65f, m_health = 1.0f, m_excited = 0.3f;
    float m_digestion = 0.0f, m_bladder = 0.0f;
    float m_lonely = 0.0f; // seconds you've been away
    float m_dying = 0.0f;  // seconds its health has been at zero
    bool m_dead = false;
};

} // namespace pet_companion
