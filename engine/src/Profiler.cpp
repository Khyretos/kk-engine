#include "kke/Profiler.h"

#include "kke/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <thread>

namespace kke {

namespace {

std::atomic<uint64_t> g_serials{ 1 };

// One thread's cached buffer for one profiler (tests make several).
struct ThreadCache {
    uint64_t serial = 0;
    void* buffer = nullptr;
};
thread_local ThreadCache t_cache;

void appendEscaped(std::string& out, const std::string& s) {
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
}

std::string num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

} // namespace

Profiler& Profiler::get() {
    static Profiler instance;
    return instance;
}

Profiler::Profiler() : m_epoch(std::chrono::steady_clock::now()), m_serial(g_serials.fetch_add(1)) {}

Profiler::~Profiler() = default;

double Profiler::nowUs() const {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - m_epoch).count();
}

uint32_t Profiler::intern(std::string_view name) {
    std::lock_guard lock(m_namesMutex);
    const std::string key(name);
    if (auto it = m_ids.find(key); it != m_ids.end()) return it->second;
    const uint32_t id = static_cast<uint32_t>(m_names.size());
    m_names.push_back(key);
    m_ids.emplace(key, id);
    return id;
}

uint32_t Profiler::moduleZone(const char* module, const char* stage) {
    {
        std::lock_guard lock(m_namesMutex);
        if (auto it = m_moduleIds.find({ module, stage }); it != m_moduleIds.end()) return it->second;
    }
    const uint32_t id = intern(std::string(module) + "." + stage);
    std::lock_guard lock(m_namesMutex);
    m_moduleIds[{ module, stage }] = id;
    return id;
}

std::string Profiler::name(uint32_t id) const {
    std::lock_guard lock(m_namesMutex);
    return id < m_names.size() ? m_names[id] : std::string("?");
}

Profiler::ThreadBuffer& Profiler::threadBuffer() {
    if (t_cache.serial == m_serial) return *static_cast<ThreadBuffer*>(t_cache.buffer);
    std::lock_guard lock(m_buffersMutex);
    auto& slot = m_byThread[std::this_thread::get_id()];
    if (!slot) {
        slot = std::make_shared<ThreadBuffer>();
        slot->thread = m_nextThread.fetch_add(1);
        slot->name = "thread " + std::to_string(slot->thread);
        m_buffers.push_back(slot);
    }
    t_cache.serial = m_serial;
    t_cache.buffer = slot.get();
    return *slot;
}

void Profiler::nameThisThread(std::string_view name) {
    ThreadBuffer& b = threadBuffer();
    std::lock_guard lock(b.mutex);
    b.name = std::string(name);
}

void Profiler::push(const Event& e) {
    if (m_eventCount.fetch_add(1, std::memory_order_relaxed) >= m_maxEvents.load(std::memory_order_relaxed)) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    ThreadBuffer& b = threadBuffer();
    std::lock_guard lock(b.mutex);
    Event copy = e;
    copy.thread = b.thread;
    b.events.push_back(copy);
}

void Profiler::zone(uint32_t id, double startUs, double durUs) {
    if (!capturing() || startUs < m_captureStartUs.load(std::memory_order_relaxed)) return;
    push({ Event::Zone, id, 0, startUs, durUs });
}

void Profiler::counter(uint32_t id, double value) {
    if (!capturing()) return;
    push({ Event::Counter, id, 0, nowUs(), value });
}

void Profiler::addModuleTime(uint32_t id, double ms) {
    m_rolling[id].frameTotal += ms;
    if (capturing()) {
        const double end = nowUs();
        zone(id, end - ms * 1000.0, ms * 1000.0);
    }
}

void Profiler::endFrame(double frameMs) {
    for (auto& [id, r] : m_rolling) {
        r.ring[r.next] = r.frameTotal;
        r.next = (r.next + 1) % kWindow;
        r.filled = std::min(r.filled + 1, kWindow);
        r.last = r.frameTotal;
        r.frameTotal = 0.0;
    }
    m_frameRing[m_frameNext] = frameMs;
    m_frameNext = (m_frameNext + 1) % kWindow;
    m_frameFilled = std::min(m_frameFilled + 1, kWindow);

    if (!capturing()) return;
    std::function<void(Profiler&)> source;
    {
        std::lock_guard lock(m_captureMutex);
        source = m_counterSource;
    }
    if (source) source(*this);
    push({ Event::Frame, 0, 0, nowUs(), frameMs });
    bool done = false;
    {
        std::lock_guard lock(m_captureMutex);
        ++m_capturedFrames;
        done = (m_options.frames > 0 && m_capturedFrames >= m_options.frames) ||
               (m_options.seconds > 0.0 && (nowUs() - m_captureStartUs.load()) / 1e6 >= m_options.seconds);
    }
    if (done) {
        std::string error;
        if (!stopCapture(&error)) log::get("Profiler")->warn("capture not saved: {}", error);
        else log::get("Profiler")->info("capture saved to {}\n{}", lastCapturePath(), lastCaptureSummary());
    }
}

std::vector<Profiler::ModuleStat> Profiler::moduleStats(size_t limit) const {
    std::vector<ModuleStat> out;
    for (const auto& [id, r] : m_rolling) {
        if (r.filled == 0) continue;
        ModuleStat s;
        s.name = name(id);
        double sum = 0.0;
        for (int i = 0; i < r.filled; ++i) {
            sum += r.ring[i];
            s.maxMs = std::max(s.maxMs, r.ring[i]);
        }
        s.avgMs = sum / r.filled;
        s.lastMs = r.last;
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const ModuleStat& a, const ModuleStat& b) { return a.avgMs > b.avgMs; });
    if (out.size() > limit) out.resize(limit);
    return out;
}

double Profiler::averageFrameMs() const {
    if (m_frameFilled == 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < m_frameFilled; ++i) sum += m_frameRing[i];
    return sum / m_frameFilled;
}

double Profiler::worstFrameMs() const {
    double worst = 0.0;
    for (int i = 0; i < m_frameFilled; ++i) worst = std::max(worst, m_frameRing[i]);
    return worst;
}

bool Profiler::startCapture(const CaptureOptions& options, std::string* error) {
    std::lock_guard lock(m_captureMutex);
    if (capturing()) {
        if (error) *error = "a capture is already running";
        return false;
    }
    {
        std::lock_guard buffers(m_buffersMutex);
        for (auto& b : m_buffers) {
            std::lock_guard each(b->mutex);
            b->events.clear();
        }
    }
    m_options = options;
    m_maxEvents = options.maxEvents;
    m_eventCount = 0;
    m_dropped = 0;
    m_capturedFrames = 0;
    m_captureStartUs = nowUs();
    m_capturing.store(true, std::memory_order_release);
    return true;
}

std::vector<Profiler::Event> Profiler::collect() const {
    std::vector<Event> all;
    std::lock_guard buffers(m_buffersMutex);
    for (const auto& b : m_buffers) {
        std::lock_guard each(b->mutex);
        all.insert(all.end(), b->events.begin(), b->events.end());
    }
    return all;
}

bool Profiler::stopCapture(std::string* error) {
    std::lock_guard lock(m_captureMutex);
    if (!capturing()) {
        if (error) *error = "no capture is running";
        return false;
    }
    m_capturing.store(false, std::memory_order_release);
    const double endUs = nowUs();
    std::vector<Event> events = collect();
    {
        std::lock_guard buffers(m_buffersMutex);
        for (auto& b : m_buffers) {
            std::lock_guard each(b->mutex);
            b->events.clear();
        }
    }

    // The per-zone table.
    std::map<uint32_t, std::vector<double>> durations;
    for (const Event& e : events)
        if (e.kind == Event::Zone) durations[e.id].push_back(e.durUs / 1000.0);
    m_lastZones.clear();
    for (auto& [id, d] : durations) {
        ZoneSummary z;
        z.name = name(id);
        z.calls = d.size();
        std::sort(d.begin(), d.end());
        for (double v : d) z.totalMs += v;
        z.avgMs = z.totalMs / static_cast<double>(d.size());
        z.p95Ms = d[std::min(d.size() - 1, static_cast<size_t>(static_cast<double>(d.size()) * 0.95))];
        z.maxMs = d.back();
        m_lastZones.push_back(std::move(z));
    }
    std::sort(m_lastZones.begin(), m_lastZones.end(), [](const ZoneSummary& a, const ZoneSummary& b) { return a.totalMs > b.totalMs; });
    std::string text;
    char line[160];
    std::snprintf(line, sizeof(line), "%-34s %8s %9s %8s %8s %8s\n", "zone", "calls", "total ms", "avg", "p95", "max");
    text += line;
    for (size_t i = 0; i < m_lastZones.size() && i < 10; ++i) {
        const ZoneSummary& z = m_lastZones[i];
        std::snprintf(line, sizeof(line), "%-34.34s %8llu %9.2f %8.3f %8.3f %8.3f\n", z.name.c_str(),
                      static_cast<unsigned long long>(z.calls), z.totalMs, z.avgMs, z.p95Ms, z.maxMs);
        text += line;
    }
    m_lastSummary = text;

    m_lastJson = buildJson(events, endUs);
    m_lastPath = m_options.path;
    if (!m_options.path.empty()) {
        std::ofstream file(m_options.path, std::ios::binary | std::ios::trunc);
        if (!file) {
            if (error) *error = "can't write " + m_options.path;
            return false;
        }
        file << m_lastJson;
        if (!file.good()) {
            if (error) *error = "writing " + m_options.path + " failed";
            return false;
        }
    }
    return true;
}

void Profiler::setCounterSource(std::function<void(Profiler&)> source) {
    std::lock_guard lock(m_captureMutex);
    m_counterSource = std::move(source);
}

Profiler::CaptureState Profiler::captureState() const {
    std::lock_guard lock(m_captureMutex);
    CaptureState s;
    s.active = capturing();
    s.path = m_options.path;
    s.elapsedSeconds = s.active ? (nowUs() - m_captureStartUs.load()) / 1e6 : 0.0;
    s.frames = m_capturedFrames;
    s.events = std::min(m_eventCount.load(), m_options.maxEvents);
    s.dropped = m_dropped.load();
    return s;
}

std::string Profiler::lastCapturePath() const {
    std::lock_guard lock(m_captureMutex);
    return m_lastPath;
}

std::string Profiler::lastCaptureSummary() const {
    std::lock_guard lock(m_captureMutex);
    return m_lastSummary;
}

std::string Profiler::traceJson() const {
    std::lock_guard lock(m_captureMutex);
    return m_lastJson;
}

std::vector<Profiler::ZoneSummary> Profiler::summary() const {
    std::lock_guard lock(m_captureMutex);
    return m_lastZones;
}

std::string Profiler::newCapturePath() {
    namespace fs = std::filesystem;
    fs::path folder = "profiles";
    if (char* pref = SDL_GetPrefPath("KreativeKompas", "profiles")) {
        folder = pref;
        SDL_free(pref);
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    return (folder / (std::string("profile-") + stamp + ".json")).string();
}

std::string Profiler::buildJson(const std::vector<Event>& events, double endUs) const {
    const double origin = m_captureStartUs.load();
    std::vector<const Event*> sorted;
    sorted.reserve(events.size());
    for (const Event& e : events) sorted.push_back(&e);
    // By thread, then start, the longer (outer) zone first: how a viewer nests them.
    std::stable_sort(sorted.begin(), sorted.end(), [](const Event* a, const Event* b) {
        if (a->thread != b->thread) return a->thread < b->thread;
        if (a->startUs != b->startUs) return a->startUs < b->startUs;
        return a->durUs > b->durUs;
    });

    std::string out = "{\"traceEvents\":[\n";
    out += "{\"ph\":\"M\",\"name\":\"process_name\",\"pid\":1,\"args\":{\"name\":\"kk-engine\"}}";
    {
        std::lock_guard buffers(m_buffersMutex);
        for (const auto& b : m_buffers) {
            std::lock_guard each(b->mutex);
            out += ",\n{\"ph\":\"M\",\"name\":\"thread_name\",\"pid\":1,\"tid\":" + std::to_string(b->thread) + ",\"args\":{\"name\":\"";
            appendEscaped(out, b->name);
            out += "\"}}";
        }
    }
    for (const Event* e : sorted) {
        const double ts = e->startUs - origin;
        out += ",\n";
        switch (e->kind) {
        case Event::Zone:
            out += "{\"name\":\"";
            appendEscaped(out, name(e->id));
            out += "\",\"cat\":\"cpu\",\"ph\":\"X\",\"ts\":" + num(ts) + ",\"dur\":" + num(e->durUs) + ",\"pid\":1,\"tid\":" + std::to_string(e->thread) + "}";
            break;
        case Event::Counter: {
            std::string n = name(e->id);
            out += "{\"name\":\"";
            appendEscaped(out, n);
            out += "\",\"ph\":\"C\",\"ts\":" + num(ts) + ",\"pid\":1,\"args\":{\"";
            appendEscaped(out, n);
            out += "\":" + num(e->durUs) + "}}";
            break;
        }
        case Event::Frame:
            out += "{\"name\":\"frame\",\"cat\":\"frame\",\"ph\":\"i\",\"s\":\"g\",\"ts\":" + num(ts) + ",\"pid\":1,\"tid\":" +
                   std::to_string(e->thread) + ",\"args\":{\"ms\":" + num(e->durUs) + "}}";
            break;
        }
    }
    out += "\n],\"displayTimeUnit\":\"ms\",\"kkeCapture\":{\"frames\":" + std::to_string(m_capturedFrames) + ",\"seconds\":" +
           num((endUs - origin) / 1e6) + ",\"events\":" + std::to_string(events.size()) + ",\"dropped\":" + std::to_string(m_dropped.load()) +
           "},\"kkeSummary\":[";
    bool first = true;
    for (const ZoneSummary& z : m_lastZones) {
        if (!first) out += ",";
        first = false;
        out += "\n{\"zone\":\"";
        appendEscaped(out, z.name);
        out += "\",\"calls\":" + std::to_string(z.calls) + ",\"totalMs\":" + num(z.totalMs) + ",\"avgMs\":" + num(z.avgMs) +
               ",\"p95Ms\":" + num(z.p95Ms) + ",\"maxMs\":" + num(z.maxMs) + "}";
    }
    out += "\n]}\n";
    return out;
}

} // namespace kke
