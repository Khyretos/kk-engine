#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace kke {

// Facial animation on blend shapes (ENG-F25, docs/FACIAL_ANIMATION.md): the
// weights of a face's morph targets from what you say you want (an expression,
// a mouth shape, a line of speech), smoothed, with automatic blinking.
// Pure logic; kke::ModelModule applies the result (setMorphWeights).
//
// Faces name their blend shapes in a few different ways, so each expression is
// a list of recipes tried in order, and the first one the model can do is used:
//   "happy"  ->  a target called happy (VRM), Fcl_ALL_Joy (VRoid), or mouthSmileLeft+Right
//                with cheekSquint (Apple ARKit's 52 shapes)
// Names match ignoring case, '_', '-' and spaces ("viseme_aa" = "visemeAA").
//
// Expressions: neutral happy angry sad surprised relaxed blink blinkLeft blinkRight
// Mouth shapes (visemes): aa ih ou ee oh, and sil (closed)
class FaceRig {
public:
    // Which morph targets the model has (morphNames(model)). Call again if it changes.
    void bind(const std::vector<std::string>& morphNames);
    bool bound() const { return !m_names.empty(); }

    // Expression by name, 0..1; it eases to the weight (emotions in about a quarter of a second,
    // mouth shapes and blinks faster). False for a name the model cannot do.
    bool setExpression(const std::string& name, float weight);
    bool canDo(const std::string& expression) const;
    // Every expression this model can do (the recipe resolved), in a fixed order.
    std::vector<std::string> expressions() const;
    // Any one morph target directly (a name the model has, whatever its spelling), 0..1.
    bool setMorph(const std::string& name, float weight);
    // Everything back to neutral.
    void clear();

    // ---- Speech
    struct VisemeKey {
        float time = 0.0f;       // seconds from the start of the line
        std::string viseme;      // aa ih ou ee oh sil
        float weight = 1.0f;
    };
    // A mouth movement for a line, from the vowels of its text spread over `seconds`
    // (a, e, i, o, u and the vowels of other Latin languages open the mouth; m, b, p close it).
    // Good enough to read as speech beside a subtitle; for exact sync supply the keys yourself.
    static std::vector<VisemeKey> lipSyncFromText(const std::string& text, float seconds);
    void say(std::vector<VisemeKey> keys);
    bool speaking() const { return m_speaking; }
    void stopSpeaking();
    // Or drive the mouth from the loudness of a voice, 0..1 (an AudioModule voice, a microphone):
    // the jaw follows the level and the vowel shape changes while it talks.
    void setSpeechLevel(float level) { m_level = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level; }

    // ---- Blinking
    void setAutoBlink(bool on) { m_autoBlink = on; }
    bool autoBlink() const { return m_autoBlink; }
    // Seconds between blinks, from..to (a blink every 2 to 6 seconds by default).
    void setBlinkInterval(float from, float to) { m_blinkFrom = from; m_blinkTo = to < from ? from : to; }
    // Start a blink now.
    void blink() { m_blinkPhase = 0.0f; }

    void update(float dt);
    // The morph weights to apply now: only the targets that are not zero, 0..1.
    const std::map<std::string, float>& weights() const { return m_weights; }
    // Bumps whenever weights() changes (so a caller can skip uploading an unchanged face).
    unsigned version() const { return m_version; }

private:
    struct Recipe {
        std::vector<std::pair<std::string, float>> morphs; // model morph name -> weight
    };
    struct Expression {
        Recipe recipe;       // resolved against the model; empty = it cannot do it
        float target = 0.0f, current = 0.0f;
        float rate = 8.0f;   // per second
    };
    static std::string key(const std::string& name);
    std::string resolve(const std::string& wanted) const; // the model's own name, or ""

    std::vector<std::string> m_names;
    std::map<std::string, std::string> m_byKey; // normalised -> model name
    std::map<std::string, Expression> m_expressions;
    std::map<std::string, std::pair<float, float>> m_raw; // model morph -> (target, current)
    std::map<std::string, float> m_weights;
    unsigned m_version = 0;

    std::vector<VisemeKey> m_keys;
    float m_speechTime = 0.0f;
    bool m_speaking = false;
    float m_level = 0.0f, m_levelSmooth = 0.0f, m_vowelClock = 0.0f;
    int m_vowel = 0;
    bool m_voiceActive = false;

    bool m_autoBlink = true;
    float m_blinkFrom = 2.0f, m_blinkTo = 6.0f, m_blinkWait = 2.5f, m_blinkPhase = -1.0f;
    unsigned m_rng = 12345u;
    float random01();
};

} // namespace kke
