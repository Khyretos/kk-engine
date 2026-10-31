#pragma once
// ScriptMusic: the music.* Lua API over kke::MusicDirector (docs/AUDIO.md "Adaptive music").
#include "kke/MusicDirector.h"

#include <functional>
#include <memory>
#include <string>

namespace kke {

class ScriptVM;

// One adaptive score for the game: segments of layered stems that change
// with parameters, transitions on the beat, stingers. The script that starts
// it owns it: when that script is unloaded the music stops and the
// definitions go.
class ScriptMusic {
public:
    // `loader` turns a file name from a script (a relative path in the game's
    // folder) into a sound, or null; `folder` is where music.load reads.
    void attach(AudioMixer* mixer, std::function<SoundHandle(const std::string&)> loader, std::string folder);
    bool attached() const { return m_backend != nullptr; }
    void bind(ScriptVM& vm);
    // Advances the director and runs the hooks (MusicBeat, MusicBar, MusicSegment, MusicEnded, MusicStinger).
    void update(float dt, ScriptVM& vm);
    void release(const std::string& source);

    MusicDirector& director() { return m_director; }

private:
    MusicDirector m_director;
    std::unique_ptr<MixerMusicBackend> m_backend;
    std::string m_folder, m_owner;
};

} // namespace kke
