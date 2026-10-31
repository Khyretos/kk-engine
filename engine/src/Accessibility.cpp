#include "kke/Accessibility.h"

#include "kke/DevTools.h"
#include "kke/EngineSettings.h"
#include "kke/Localization.h"
#include "kke/Subtitles.h"

#include <glm/glm.hpp>

#include <algorithm>

#if defined(__linux__) || defined(__APPLE__)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
extern char** environ;
#endif

namespace kke {

namespace {

// Rows as written in the paper, to a glm matrix (which is filled by column).
glm::mat3 fromRows(float a, float b, float c, float d, float e, float f, float g, float h, float i) {
    return glm::transpose(glm::mat3(a, b, c, d, e, f, g, h, i));
}

// Machado, Oliveira and Fernandes 2009, severity 1.0, for linear RGB: what a
// person with that deficiency sees.
glm::mat3 simulation(const std::string& mode) {
    if (mode == "protanopia") return fromRows(0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f);
    if (mode == "deuteranopia") return fromRows(0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f);
    return fromRows(1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f); // tritanopia
}

// Where the error (what the simulation lost) is put back: into the channels the person still tells apart.
glm::mat3 errorShift(const std::string& mode) {
    if (mode == "tritanopia") return fromRows(1.0f, 0.0f, 0.7f, 0.0f, 1.0f, 0.7f, 0.0f, 0.0f, 0.0f);
    return fromRows(0.0f, 0.0f, 0.0f, 0.7f, 1.0f, 0.0f, 0.7f, 0.0f, 1.0f); // protan and deutan: into green and blue
}

} // namespace

Accessibility& Accessibility::get() {
    static Accessibility instance;
    return instance;
}

void Accessibility::set(const Options& options) {
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard lock(m_mutex);
        const bool changed = options.colorBlind != m_options.colorBlind || options.colorBlindStrength != m_options.colorBlindStrength ||
                             options.highContrast != m_options.highContrast || options.reduceMotion != m_options.reduceMotion ||
                             options.screenReader != m_options.screenReader;
        m_options = options;
        if (!changed) return;
        for (auto& [id, cb] : m_listeners) callbacks.push_back(cb);
    }
    for (auto& cb : callbacks) cb();
}

void Accessibility::apply(const EngineSettings& settings) {
    Options o;
    o.colorBlind = settings.accessibility.colorBlind;
    o.colorBlindStrength = settings.accessibility.colorBlindStrength;
    o.highContrast = settings.accessibility.highContrast;
    o.reduceMotion = settings.accessibility.reduceMotion;
    o.screenReader = settings.accessibility.screenReader;
    set(o);
}

glm::mat3 Accessibility::colorMatrix(const std::string& mode, float strength) {
    const glm::mat3 identity(1.0f);
    if (mode != "protanopia" && mode != "deuteranopia" && mode != "tritanopia") return identity;
    const float s = std::clamp(strength, 0.0f, 1.0f);
    const glm::mat3 correction = identity + errorShift(mode) * (identity - simulation(mode));
    return identity + (correction - identity) * s;
}

std::vector<glm::vec4> Accessibility::colorMatrixRows(const std::string& mode, float strength) {
    const glm::mat3 m = colorMatrix(mode, strength);
    const bool on = mode == "protanopia" || mode == "deuteranopia" || mode == "tritanopia";
    const float flag = on && strength > 0.0f ? 1.0f : 0.0f;
    // glm is column-major: row r is (m[0][r], m[1][r], m[2][r]).
    return { glm::vec4(m[0][0], m[1][0], m[2][0], flag), glm::vec4(m[0][1], m[1][1], m[2][1], 0.0f), glm::vec4(m[0][2], m[1][2], m[2][2], 0.0f) };
}

glm::vec3 Accessibility::correct(const glm::vec3& linear, const std::string& mode, float strength) {
    return glm::clamp(colorMatrix(mode, strength) * linear, glm::vec3(0.0f), glm::vec3(1.0f));
}

void Accessibility::setAnnouncer(Announcer announcer) {
    std::lock_guard lock(m_mutex);
    m_announcer = std::move(announcer);
}

bool Accessibility::announce(const std::string& text, bool interrupt) {
    if (text.empty()) return false;
    Announcer announcer;
    bool on = false;
    {
        std::lock_guard lock(m_mutex);
        m_recent.push_back(text);
        if (m_recent.size() > 16) m_recent.erase(m_recent.begin());
        announcer = m_announcer;
        on = m_options.screenReader;
    }
    if (!on || !announcer) return false;
    announcer(text, interrupt);
    return true;
}

std::vector<std::string> Accessibility::recent() const {
    std::lock_guard lock(m_mutex);
    return m_recent;
}

Accessibility::Announcer Accessibility::systemSpeech() {
#if defined(__linux__) || defined(__APPLE__)
#if defined(__APPLE__)
    const char* program = "say";
#else
    const char* program = "spd-say";
#endif
    auto run = [program](std::vector<std::string> args, bool wait) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program));
        for (std::string& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid = 0;
        // The text is an argument, never part of a command line: nothing in it can be run.
        if (posix_spawnp(&pid, program, nullptr, nullptr, argv.data(), environ) != 0) return false;
        if (wait) waitpid(pid, nullptr, 0);
        else std::thread([pid] { waitpid(pid, nullptr, 0); }).detach(); // no zombie
        return true;
    };
    // Is there one at all? A run with --help costs nothing and tells.
    static const bool available = [&] {
#if defined(__APPLE__)
        return access("/usr/bin/say", X_OK) == 0;
#else
        return run({ "--version" }, true);
#endif
    }();
    if (!available) return {};
    return [run](const std::string& text, bool interrupt) {
#if defined(__APPLE__)
        (void)interrupt; // say has no way to cut itself off
        run({ text }, false);
#else
        if (interrupt) run({ "-C" }, true); // cancel what is being said
        run({ "--", text }, false);
#endif
    };
#else
    return {};
#endif
}

int Accessibility::onChange(std::function<void()> callback) {
    std::lock_guard lock(m_mutex);
    const int id = m_nextListener++;
    m_listeners[id] = std::move(callback);
    return id;
}

void Accessibility::removeOnChange(int id) {
    std::lock_guard lock(m_mutex);
    m_listeners.erase(id);
}

void applyTextAndAccessibility(const EngineSettings& settings) {
    Accessibility::get().apply(settings);
    Subtitles::get().setEnabled(settings.text.subtitles);
    Localization& loc = Localization::get();
    if (const char* forced = dev::env("KKE_LANGUAGE"); forced && *forced) loc.setLanguage(forced);
    else if (settings.text.language == "auto") loc.useSystemLanguage();
    else loc.setLanguage(settings.text.language);
}

} // namespace kke

