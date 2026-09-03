#pragma once

#include <exception>
#include <string>

namespace kke {

// Where a fault most likely originates — used to frame the Emergency Log
// differently depending on the answer ("check your script" vs. "this is
// worth reporting upstream"). Unknown is the honest default for a plain
// std::exception the engine had no chance to classify — see EngineError
// below for how a module (or, once it exists, the Lua binding layer) can
// be specific instead.
enum class ErrorSource {
    Unknown,
    Engine, // a bug in this engine's own C++ code
    Script  // a bug in game/script code — the case a non-programmer needs a plain-language message for
};

// The one sanctioned way for a module — or, once the Lua scripting layer
// exists, script code running inside it — to throw an error carrying
// enough structure for the Emergency Log (see DebugControlModule) to
// actually help someone who isn't a C++ programmer: a plain-language
// message, WHERE the problem is (file/line, when known), and WHO's
// likely at fault.
//
// A plain std::runtime_error still works everywhere this is caught
// (Application::safeInvoke) — it just can't offer any of this, and gets
// treated as ErrorSource::Unknown with its own message standing in for
// both the friendly and technical text. Prefer throwing this over a bare
// std::runtime_error wherever you can name the actual problem in plain
// language, especially in anything a non-programmer might trigger.
//
// Concretely, the difference this makes: a raw exception might say
// something like `basic_string::at: __n (which is 5) >= this->size()
// (which is 3)` — precise, and meaningless to someone who wrote a script
// and has never heard of `basic_string::at`. The same failure, reported
// through EngineError, can say "line 14: tried to read ingredient #5,
// but this recipe only has 3" — the technical message is still there
// (what() returns it, and it's shown as a secondary detail in the
// Emergency Log), but the thing put in front of the person is the part
// they can actually act on.
class EngineError : public std::exception {
public:
    EngineError(std::string friendlyMessage, std::string technicalMessage,
                ErrorSource source = ErrorSource::Engine,
                std::string file = "", int line = 0)
        : m_friendlyMessage(std::move(friendlyMessage)),
          m_technicalMessage(std::move(technicalMessage)),
          m_source(source), m_file(std::move(file)), m_line(line) {}

    // std::exception::what() returns the TECHNICAL message — this keeps
    // EngineError behaving exactly like any other std::exception for
    // code that doesn't know it's special (generic catch blocks, plain
    // logging). Call friendlyMessage() explicitly for the plain-language
    // version.
    const char* what() const noexcept override { return m_technicalMessage.c_str(); }

    const std::string& friendlyMessage() const { return m_friendlyMessage; }
    ErrorSource source() const { return m_source; }
    const std::string& file() const { return m_file; }
    int line() const { return m_line; }
    bool hasLocation() const { return !m_file.empty(); }

private:
    std::string m_friendlyMessage;
    std::string m_technicalMessage;
    ErrorSource m_source;
    std::string m_file;
    int m_line;
};

} // namespace kke

// Convenience macros that splice in __FILE__/__LINE__ from the call site
// — nothing magic beyond that, just less boilerplate for the common case
// of "I know exactly what's wrong and where." Use KKE_SCRIPT_ERROR for
// anything a game/script author's own mistake would trigger (bad data,
// a missing asset, an out-of-range value they supplied); KKE_ENGINE_ERROR
// for a bug in the engine's own code.
#define KKE_SCRIPT_ERROR(friendlyMessage, technicalMessage) \
    ::kke::EngineError((friendlyMessage), (technicalMessage), ::kke::ErrorSource::Script, __FILE__, __LINE__)
#define KKE_ENGINE_ERROR(friendlyMessage, technicalMessage) \
    ::kke::EngineError((friendlyMessage), (technicalMessage), ::kke::ErrorSource::Engine, __FILE__, __LINE__)
