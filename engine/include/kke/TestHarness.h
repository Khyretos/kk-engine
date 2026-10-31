#pragma once

// Game test harness (ENG-F69): record a play session's keyboard, mouse and
// text input frame by frame, play it back, take screenshots at chosen
// frames and compare them with reference pictures.
//
// The pieces here are pure (no window, no GPU) so they are unit-tested;
// Application wires them to the real loop through developer switches
// (docs/TEST_HARNESS.md):
//
//   KKE_FIXED_DT=60            every frame advances 1/60 s (or "0.0166"),
//                              whatever the wall clock says: runs repeat
//   KKE_INPUT_RECORD=run.tape  write the session's input here on exit
//   KKE_INPUT_PLAYBACK=run.tape  push it back in, frame for frame
//   KKE_SHOTS=120:a.png,300:b.png  screenshots (PNG) at those frames
//   KKE_QUIT_AT_FRAME=360      quit after that many frames
//
// Gamepads are polled, not evented, so a tape doesn't hold them: use
// KKE_VIRTUAL_PAD_SCRIPT (InputModule.h) for pad input in a test.

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kke::harness {

// One recorded input event and the frame it was polled in.
struct TapeEvent {
    uint64_t frame = 0;
    SDL_Event event{};
    std::string text; // SDL_EVENT_TEXT_INPUT's text (the event points here once pushed)
};

// Whether a tape records this event (keys, text, mouse buttons, motion
// and wheel, fingers).
bool isTapeEvent(const SDL_Event& e);

// One line of a tape: "<frame> <kind> <fields...>". Empty for an event a
// tape doesn't hold.
std::string encodeEvent(uint64_t frame, const SDL_Event& e);
// The reverse; nullopt (and a reason) for a line that isn't one.
std::optional<TapeEvent> decodeEvent(const std::string& line, std::string* error = nullptr);

// A recorded session: events in frame order.
class InputTape {
public:
    void record(uint64_t frame, const SDL_Event& e);
    const std::vector<TapeEvent>& events() const { return m_events; }
    uint64_t lastFrame() const { return m_events.empty() ? 0 : m_events.back().frame; }

    // Text file, one event per line after a "kke-input-tape 1" header;
    // lines starting with '#' are comments.
    std::string serialize() const;
    bool parse(const std::string& text, std::string* error = nullptr);
    bool save(const std::string& path) const;
    bool load(const std::string& path, std::string* error = nullptr);

    // Playback: the events of `frame`, each once, in recorded order.
    // Events of frames already passed come out too (none are dropped).
    std::vector<TapeEvent*> due(uint64_t frame);
    bool finished() const { return m_next >= m_events.size(); }

private:
    std::vector<TapeEvent> m_events;
    size_t m_next = 0;
};

// KKE_SHOTS: "<frame>:<file>,<frame>:<file>", in any order (sorted on
// return). nullopt and a reason for a malformed list.
struct ShotPlan {
    uint64_t frame = 0;
    std::string path;
};
std::optional<std::vector<ShotPlan>> parseShotPlan(const std::string& spec, std::string* error = nullptr);

// KKE_FIXED_DT: "60" (frames a second, anything above 1) or "0.0166"
// (seconds). nullopt for nonsense, zero or negative values.
std::optional<float> parseFixedDt(const std::string& spec);

// How two RGBA pictures (4 bytes a pixel) differ. A channel counts as
// different when it is more than `tolerance` (0-255) away; small
// tolerances absorb dithering and driver rounding between GPUs.
struct ImageDiff {
    bool sameSize = true;
    uint32_t width = 0, height = 0;
    double meanAbs = 0.0;            // mean channel difference, 0-255 (RGB only)
    int maxAbs = 0;                  // the largest channel difference
    double differingFraction = 0.0;  // pixels with any RGB channel over tolerance
    double psnr = 0.0;               // dB, infinity for identical pictures
};
ImageDiff compareImages(const std::vector<uint8_t>& a, uint32_t aw, uint32_t ah, const std::vector<uint8_t>& b, uint32_t bw, uint32_t bh,
                        int tolerance = 8);

// Pass/fail for a test: same size, at most `maxFraction` of the pixels
// differ beyond the tolerance.
bool imagesMatch(const ImageDiff& d, double maxFraction = 0.002);

// A picture of where they differ: the reference dimmed to a quarter, the
// differing pixels in bright magenta. Same size as `a` (empty if sizes differ).
std::vector<uint8_t> diffImage(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t w, uint32_t h, int tolerance = 8);

} // namespace kke::harness
