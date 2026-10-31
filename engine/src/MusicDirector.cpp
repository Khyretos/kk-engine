#include "kke/MusicDirector.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

using nlohmann::json;

const char* musicWhenName(MusicWhen w) {
    switch (w) {
    case MusicWhen::Immediate: return "immediate";
    case MusicWhen::NextBeat: return "next_beat";
    case MusicWhen::NextBar: return "next_bar";
    case MusicWhen::Grid: return "grid";
    case MusicWhen::EndOfSegment: return "end";
    }
    return "next_bar";
}

bool musicWhenFromName(const std::string& n, MusicWhen& out) {
    if (n == "immediate" || n == "now") out = MusicWhen::Immediate;
    else if (n == "next_beat" || n == "beat") out = MusicWhen::NextBeat;
    else if (n == "next_bar" || n == "bar") out = MusicWhen::NextBar;
    else if (n == "grid") out = MusicWhen::Grid;
    else if (n == "end" || n == "end_of_segment") out = MusicWhen::EndOfSegment;
    else return false;
    return true;
}

// ---- the mixer as a backend ---------------------------------------------------------

std::vector<uint32_t> MixerMusicBackend::start(const std::vector<MusicVoiceSpec>& voices, uint32_t reference, double atSeconds,
                                               const std::vector<uint32_t>& fadeOut, float fadeOutSeconds) {
    std::vector<VoiceDesc> descs;
    std::vector<size_t> index;
    for (size_t i = 0; i < voices.size(); ++i) {
        const SoundHandle h = m_load ? m_load(voices[i].sound) : SoundHandle();
        if (!h) continue;
        VoiceDesc d;
        d.sound = h;
        d.spatial = false;
        d.gain = 1.0f; // the level is the voice's fade (setGain), which the group can change without a click
        d.loop = voices[i].loop;
        d.category = SoundCategory::Music;
        d.priority = 100.0f;
        d.reverbSend = 0.0f;
        descs.push_back(d);
        index.push_back(i);
    }
    AudioMixer::VoiceSchedule when;
    when.reference = reference;
    when.atSeconds = atSeconds;
    when.fadeOut = fadeOut;
    when.fadeOutSeconds = fadeOutSeconds;
    const std::vector<uint32_t> ids = m_mixer.playScheduled(descs, when);
    std::vector<uint32_t> out(voices.size(), 0);
    for (size_t k = 0; k < ids.size(); ++k) {
        out[index[k]] = ids[k];
        if (!ids[k]) continue;
        const MusicVoiceSpec& spec = voices[index[k]];
        if (spec.fadeIn > 0.0f) { // from silence when it starts (a waiting voice does not fade until it plays)
            m_mixer.fade(ids[k], 0.0f, 0.0f);
            m_mixer.fade(ids[k], spec.gain, spec.fadeIn);
        } else {
            m_mixer.fade(ids[k], spec.gain, 0.0f);
        }
    }
    return out;
}

void MixerMusicBackend::stop(uint32_t voice, float fadeSeconds) {
    if (fadeSeconds > 0.0f) m_mixer.fade(voice, 0.0f, fadeSeconds, true);
    else m_mixer.stop(voice);
}

// ---- definitions ---------------------------------------------------------------------

namespace {
bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}
} // namespace

bool MusicDirector::defineSegment(const MusicSegmentDef& s, std::string* error) {
    if (s.name.empty()) return fail(error, "a segment needs a name");
    if (!std::isfinite(s.bpm) || s.bpm < 20.0f || s.bpm > 400.0f) return fail(error, "segment '" + s.name + "': bpm is 20 to 400");
    if (s.beatsPerBar < 1 || s.beatsPerBar > 16) return fail(error, "segment '" + s.name + "': beatsPerBar is 1 to 16");
    if (s.bars < 0 || s.bars > 4096) return fail(error, "segment '" + s.name + "': bars is 0 to 4096");
    if (!std::isfinite(s.gain) || s.gain < 0.0f) return fail(error, "segment '" + s.name + "': gain must be 0 or more");
    if (s.layers.empty()) return fail(error, "segment '" + s.name + "' has no layers");
    if (s.layers.size() > kMaxLayers) return fail(error, "segment '" + s.name + "': at most " + std::to_string(kMaxLayers) + " layers");
    for (size_t i = 0; i < s.layers.size(); ++i) {
        const MusicLayerDef& l = s.layers[i];
        const std::string where = "segment '" + s.name + "' layer " + std::to_string(i + 1);
        if (l.sound.empty()) return fail(error, where + " has no sound");
        if (l.parameter.empty()) return fail(error, where + " has no parameter");
        for (float v : { l.from, l.full, l.fadeFrom, l.to, l.gain, l.fade })
            if (!std::isfinite(v)) return fail(error, where + ": numbers must be finite");
        if (l.gain < 0.0f || l.fade < 0.0f) return fail(error, where + ": gain and fade must be 0 or more");
    }
    if (m_segments.count(s.name) == 0 && m_segments.size() >= kMaxSegments) return fail(error, "more than " + std::to_string(kMaxSegments) + " segments");
    m_segments[s.name] = s;
    return true;
}

bool MusicDirector::defineTransition(const MusicTransitionDef& t, std::string* error) {
    if (t.from.empty() || t.to.empty()) return fail(error, "a transition needs from and to (\"*\" for any)");
    if (t.gridBars < 1 || t.gridBars > 256) return fail(error, "gridBars is 1 to 256");
    if (!std::isfinite(t.fadeOut) || !std::isfinite(t.fadeIn) || t.fadeOut < 0.0f || t.fadeIn < 0.0f) return fail(error, "fades must be 0 or more");
    if (m_transitions.size() >= kMaxTransitions) return fail(error, "too many transitions");
    m_transitions.push_back(t);
    return true;
}

bool MusicDirector::defineStinger(const MusicStingerDef& s, std::string* error) {
    if (s.name.empty() || s.sound.empty()) return fail(error, "a stinger needs a name and a sound");
    if (!std::isfinite(s.gain) || s.gain < 0.0f) return fail(error, "stinger '" + s.name + "': gain must be 0 or more");
    if (s.when != MusicWhen::Immediate && s.when != MusicWhen::NextBeat && s.when != MusicWhen::NextBar)
        return fail(error, "stinger '" + s.name + "': when is immediate, next_beat or next_bar");
    if (m_stingers.count(s.name) == 0 && m_stingers.size() >= kMaxStingers) return fail(error, "too many stingers");
    m_stingers[s.name] = s;
    return true;
}

bool MusicDirector::fromJson(const json& j, std::string* error) {
    if (!j.is_object()) return fail(error, "music is an object {segments, transitions, stingers}");
    auto number = [&](const json& o, const char* key, float& out, const std::string& path) {
        const auto it = o.find(key);
        if (it == o.end()) return true;
        if (!it->is_number()) return fail(error, path + ": " + key + " must be a number");
        out = it->get<float>();
        return true;
    };
    auto text = [&](const json& o, const char* key, std::string& out, const std::string& path) {
        const auto it = o.find(key);
        if (it == o.end()) return true;
        if (!it->is_string()) return fail(error, path + ": " + key + " must be text");
        out = it->get<std::string>();
        return true;
    };
    auto whenOf = [&](const json& o, MusicWhen& out, const std::string& path) {
        const auto it = o.find("when");
        if (it == o.end()) return true;
        if (!it->is_string() || !musicWhenFromName(it->get<std::string>(), out))
            return fail(error, path + ": when is immediate, next_beat, next_bar, grid or end");
        return true;
    };
    if (const auto segs = j.find("segments"); segs != j.end() && !(segs->is_object() && segs->empty())) {
        if (!segs->is_array()) return fail(error, "segments is a list");
        for (size_t i = 0; i < segs->size(); ++i) {
            const json& sj = (*segs)[i];
            const std::string path = "segments[" + std::to_string(i) + "]";
            if (!sj.is_object()) return fail(error, path + " is an object");
            MusicSegmentDef s;
            float bpb = static_cast<float>(s.beatsPerBar), bars = 0.0f;
            if (!text(sj, "name", s.name, path) || !number(sj, "bpm", s.bpm, path) || !number(sj, "beatsPerBar", bpb, path) || !number(sj, "bars", bars, path) ||
                !number(sj, "gain", s.gain, path) || !text(sj, "next", s.next, path))
                return false;
            s.beatsPerBar = static_cast<int>(bpb);
            s.bars = static_cast<int>(bars);
            if (const auto lp = sj.find("loop"); lp != sj.end() && lp->is_boolean()) s.loop = lp->get<bool>();
            if (const auto ls = sj.find("layers"); ls != sj.end() && !(ls->is_object() && ls->empty())) {
                if (!ls->is_array()) return fail(error, path + ": layers is a list");
                for (size_t k = 0; k < ls->size(); ++k) {
                    const json& lj = (*ls)[k];
                    const std::string lp = path + ".layers[" + std::to_string(k) + "]";
                    if (!lj.is_object()) return fail(error, lp + " is an object");
                    MusicLayerDef l;
                    if (!text(lj, "sound", l.sound, lp) || !text(lj, "parameter", l.parameter, lp) || !number(lj, "from", l.from, lp) || !number(lj, "full", l.full, lp) ||
                        !number(lj, "fadeFrom", l.fadeFrom, lp) || !number(lj, "to", l.to, lp) || !number(lj, "gain", l.gain, lp) || !number(lj, "fade", l.fade, lp))
                        return false;
                    if (lj.contains("from") && !lj.contains("full")) l.full = l.from; // a switch unless a ramp is given
                    s.layers.push_back(std::move(l));
                }
            }
            std::string why;
            if (!defineSegment(s, &why)) return fail(error, path + ": " + why);
        }
    }
    if (const auto trs = j.find("transitions"); trs != j.end() && !(trs->is_object() && trs->empty())) {
        if (!trs->is_array()) return fail(error, "transitions is a list");
        for (size_t i = 0; i < trs->size(); ++i) {
            const json& tj = (*trs)[i];
            const std::string path = "transitions[" + std::to_string(i) + "]";
            if (!tj.is_object()) return fail(error, path + " is an object");
            MusicTransitionDef t;
            float grid = static_cast<float>(t.gridBars);
            if (!text(tj, "from", t.from, path) || !text(tj, "to", t.to, path) || !whenOf(tj, t.when, path) || !number(tj, "gridBars", grid, path) ||
                !number(tj, "fadeOut", t.fadeOut, path) || !number(tj, "fadeIn", t.fadeIn, path) || !text(tj, "via", t.via, path))
                return false;
            t.gridBars = static_cast<int>(grid);
            std::string why;
            if (!defineTransition(t, &why)) return fail(error, path + ": " + why);
        }
    }
    if (const auto sts = j.find("stingers"); sts != j.end() && !(sts->is_object() && sts->empty())) {
        if (!sts->is_array()) return fail(error, "stingers is a list");
        for (size_t i = 0; i < sts->size(); ++i) {
            const json& sj = (*sts)[i];
            const std::string path = "stingers[" + std::to_string(i) + "]";
            if (!sj.is_object()) return fail(error, path + " is an object");
            MusicStingerDef s;
            if (!text(sj, "name", s.name, path) || !text(sj, "sound", s.sound, path) || !number(sj, "gain", s.gain, path) || !whenOf(sj, s.when, path)) return false;
            std::string why;
            if (!defineStinger(s, &why)) return fail(error, path + ": " + why);
        }
    }
    return true;
}

const MusicSegmentDef* MusicDirector::segment(const std::string& name) const {
    const auto it = m_segments.find(name);
    return it == m_segments.end() ? nullptr : &it->second;
}

void MusicDirector::clear() {
    stop(0.0f);
    m_segments.clear();
    m_transitions.clear();
    m_stingers.clear();
    m_params.clear();
    m_events.clear();
}

// ---- playing -------------------------------------------------------------------------

void MusicDirector::setParameter(const std::string& name, float value) {
    if (std::isfinite(value)) m_params[name] = value;
}

float MusicDirector::parameter(const std::string& name) const {
    const auto it = m_params.find(name);
    return it == m_params.end() ? 0.0f : it->second;
}

float MusicDirector::layerTarget(const MusicSegmentDef& s, const MusicLayerDef& l) const {
    const float v = parameter(l.parameter);
    float rise = 1.0f;
    if (v < l.from) rise = 0.0f;
    else if (l.full > l.from) rise = std::clamp((v - l.from) / (l.full - l.from), 0.0f, 1.0f);
    float fall = 1.0f;
    if (v > l.fadeFrom) fall = l.to > l.fadeFrom ? std::clamp(1.0f - (v - l.fadeFrom) / (l.to - l.fadeFrom), 0.0f, 1.0f) : 0.0f;
    return l.gain * s.gain * rise * fall;
}

std::vector<MusicVoiceSpec> MusicDirector::specsFor(const MusicSegmentDef& s, float fadeIn, std::vector<float>& gains) const {
    std::vector<MusicVoiceSpec> specs;
    gains.clear();
    for (const MusicLayerDef& l : s.layers) {
        MusicVoiceSpec v;
        v.sound = l.sound;
        v.gain = layerTarget(s, l);
        v.loop = s.loop;
        v.fadeIn = fadeIn;
        gains.push_back(v.gain);
        specs.push_back(std::move(v));
    }
    return specs;
}

const MusicTransitionDef* MusicDirector::findTransition(const std::string& from, const std::string& to) const {
    for (const MusicTransitionDef& t : m_transitions)
        if ((t.from == "*" || t.from == from) && (t.to == "*" || t.to == to)) return &t;
    return nullptr;
}

double MusicDirector::segmentLength(const MusicSegmentDef& s, uint32_t voice) const {
    if (s.bars > 0) return static_cast<double>(s.bars) * s.beatsPerBar * 60.0 / static_cast<double>(s.bpm);
    return m_backend ? m_backend->length(voice) : 0.0;
}

double MusicDirector::position() const { return m_backend && !m_voices.empty() ? m_backend->position(m_voices[0]) : 0.0; }

// The next moment of `when` after `pos`, in seconds into the segment.
double MusicDirector::boundary(const MusicSegmentDef& s, MusicWhen when, int gridBars, double pos, double length) const {
    const double beat = 60.0 / static_cast<double>(s.bpm);
    const double bar = beat * s.beatsPerBar;
    double unit = bar;
    switch (when) {
    case MusicWhen::Immediate: return pos;
    case MusicWhen::EndOfSegment: return length;
    case MusicWhen::NextBeat: unit = beat; break;
    case MusicWhen::NextBar: unit = bar; break;
    case MusicWhen::Grid: unit = bar * std::max(1, gridBars); break;
    }
    double at = (std::floor(pos / unit + 1e-9) + 1.0) * unit;
    if (at - pos < static_cast<double>(minLead)) at += unit;
    if (length > 0.0 && at > length) at = length; // a grid longer than the segment: its end
    return at;
}

void MusicDirector::push(Event e) {
    if (m_events.size() < 4096) m_events.push_back(std::move(e));
}

void MusicDirector::adopt(const Stage& st) {
    m_current = st.name;
    m_voices = st.ids;
    m_layerGain = st.gains;
    m_sentGain = st.gains;
    m_beatIndex = -1;
    m_beat = 0.0f;
    m_bar = 0;
    Event e;
    e.kind = Event::Kind::Segment;
    e.name = st.name;
    push(std::move(e));
}

void MusicDirector::cancelStages() {
    if (m_backend)
        for (const Stage& st : m_stages)
            for (uint32_t id : st.ids)
                if (id) m_backend->stop(id, 0.0f);
    m_stages.clear();
}

bool MusicDirector::playNow(const std::string& name, float fadeIn, std::string* error) {
    if (!m_backend) return fail(error, "no music backend");
    const MusicSegmentDef* s = segment(name);
    if (!s) return fail(error, "no music segment called '" + name + "'");
    cancelStages();
    std::vector<float> gains;
    const std::vector<MusicVoiceSpec> specs = specsFor(*s, std::max(fadeIn, 0.0f), gains);
    const std::vector<uint32_t> ids = m_backend->start(specs, 0, 0.0, m_voices, 0.3f);
    if (ids.empty() || std::find(ids.begin(), ids.end(), 0u) != ids.end()) {
        for (uint32_t id : ids)
            if (id) m_backend->stop(id, 0.0f);
        return fail(error, "segment '" + name + "': a layer's sound could not be played");
    }
    Stage st{ name, ids, gains };
    adopt(st);
    return true;
}

bool MusicDirector::play(const std::string& name, std::string* error) {
    if (!m_backend) return fail(error, "no music backend");
    const MusicSegmentDef* target = segment(name);
    if (!target) return fail(error, "no music segment called '" + name + "'");
    update(0.0f); // a transition that has begun counts as done
    if (m_current.empty() && m_stages.empty()) return playNow(name, 0.0f, error);
    if (m_stages.empty() && name == m_current) return true;
    if (!m_stages.empty() && m_stages.back().name == name) return true; // already on its way
    const bool backToCurrent = name == m_current;
    cancelStages();
    if (backToCurrent) return true; // the waiting one was dropped: staying on this
    if (m_current.empty()) return playNow(name, 0.0f, error);

    const MusicSegmentDef& from = m_segments.at(m_current);
    MusicTransitionDef rule;
    if (const MusicTransitionDef* t = findTransition(m_current, name)) rule = *t;
    const uint32_t ref = m_voices.empty() ? 0 : m_voices[0];
    const double pos = ref ? m_backend->position(ref) : 0.0;
    const double length = segmentLength(from, ref);
    const bool now = rule.when == MusicWhen::Immediate || ref == 0;
    const double at = now ? 0.0 : boundary(from, rule.when, rule.gridBars, pos, length);

    const MusicSegmentDef* via = nullptr;
    if (!rule.via.empty()) {
        via = segment(rule.via);
        if (!via) return fail(error, "the transition to '" + name + "' goes via '" + rule.via + "', which is not a segment");
    }

    std::vector<Stage> stages;
    auto startStage = [&](const MusicSegmentDef& seg, float fadeIn, uint32_t reference, double atSeconds, const std::vector<uint32_t>& fadeOut,
                          float fadeOutSeconds, bool loop) {
        std::vector<float> gains;
        std::vector<MusicVoiceSpec> specs = specsFor(seg, fadeIn, gains);
        for (MusicVoiceSpec& v : specs) v.loop = loop;
        Stage st{ seg.name, m_backend->start(specs, reference, atSeconds, fadeOut, fadeOutSeconds), gains };
        const bool ok = !st.ids.empty() && std::find(st.ids.begin(), st.ids.end(), 0u) == st.ids.end();
        if (!ok)
            for (uint32_t id : st.ids)
                if (id) m_backend->stop(id, 0.0f);
        if (ok) stages.push_back(std::move(st));
        return ok;
    };
    bool ok = true;
    if (via) {
        // the bridge starts at the boundary; the target starts when the bridge ends
        ok = startStage(*via, rule.fadeIn, now ? 0 : ref, at, m_voices, rule.fadeOut, false);
        if (ok) {
            const uint32_t bridge = stages.back().ids[0];
            ok = startStage(*target, 0.0f, bridge, segmentLength(*via, bridge), {}, 0.0f, target->loop);
        }
    } else {
        ok = startStage(*target, rule.fadeIn, now ? 0 : ref, at, m_voices, rule.fadeOut, target->loop);
    }
    if (!ok) {
        for (const Stage& st : stages)
            for (uint32_t id : st.ids)
                if (id) m_backend->stop(id, 0.0f);
        return fail(error, "segment '" + name + "': a layer's sound could not be played");
    }
    m_stages = std::move(stages);
    return true;
}

void MusicDirector::stop(float fade) {
    cancelStages();
    if (m_backend)
        for (uint32_t id : m_voices)
            if (id) m_backend->stop(id, fade);
    if (!m_current.empty()) {
        Event e;
        e.kind = Event::Kind::Ended;
        e.name = m_current;
        push(std::move(e));
    }
    m_current.clear();
    m_voices.clear();
    m_layerGain.clear();
    m_sentGain.clear();
    m_beatIndex = -1;
}

bool MusicDirector::stinger(const std::string& name, std::string* error) {
    if (!m_backend) return fail(error, "no music backend");
    const auto it = m_stingers.find(name);
    if (it == m_stingers.end()) return fail(error, "no stinger called '" + name + "'");
    const MusicStingerDef& def = it->second;
    uint32_t ref = 0;
    double at = 0.0;
    if (!m_current.empty() && !m_voices.empty() && def.when != MusicWhen::Immediate) {
        const MusicSegmentDef& s = m_segments.at(m_current);
        ref = m_voices[0];
        at = boundary(s, def.when, 1, m_backend->position(ref), segmentLength(s, ref));
    }
    MusicVoiceSpec spec;
    spec.sound = def.sound;
    spec.gain = def.gain;
    spec.loop = false;
    const std::vector<uint32_t> ids = m_backend->start({ spec }, ref, at, {}, 0.0f);
    if (ids.empty() || ids[0] == 0) return fail(error, "stinger '" + name + "': the sound could not be played");
    Event e;
    e.kind = Event::Kind::Stinger;
    e.name = name;
    push(std::move(e));
    return true;
}

void MusicDirector::update(float) {
    if (!m_backend) return;
    // transitions that have begun
    while (!m_stages.empty()) {
        const Stage& st = m_stages.front();
        const uint32_t first = st.ids[0];
        if (m_backend->position(first) > 0.0 || !m_backend->playing(first)) {
            adopt(st);
            m_stages.erase(m_stages.begin());
        } else {
            break;
        }
    }
    if (m_current.empty() || m_voices.empty()) return;
    const MusicSegmentDef& def = m_segments.at(m_current);
    // a segment that does not loop has ended
    if (!def.loop && m_stages.empty() && !m_backend->playing(m_voices[0])) {
        Event e;
        e.kind = Event::Kind::Ended;
        e.name = m_current;
        push(std::move(e));
        const std::string next = def.next;
        m_current.clear();
        m_voices.clear();
        m_layerGain.clear();
        m_sentGain.clear();
        if (!next.empty()) playNow(next, 0.0f);
        return;
    }
    // beats and bars
    const double beatLength = 60.0 / static_cast<double>(def.bpm);
    const double beats = m_backend->position(m_voices[0]) / beatLength;
    m_beat = static_cast<float>(beats);
    const int index = static_cast<int>(std::floor(beats));
    if (index != m_beatIndex) {
        m_beatIndex = index;
        Event e;
        e.beat = index % def.beatsPerBar;
        e.bar = index / def.beatsPerBar;
        m_bar = e.bar;
        e.name = m_current;
        e.kind = Event::Kind::Beat;
        push(e);
        if (e.beat == 0) {
            e.kind = Event::Kind::Bar;
            push(e);
        }
    }
    // layers follow their parameters
    for (size_t i = 0; i < def.layers.size() && i < m_voices.size() && i < m_sentGain.size(); ++i) { // (a segment redefined while it plays keeps its old layer count)
        const float target = layerTarget(def, def.layers[i]);
        m_layerGain[i] = target;
        if (std::fabs(target - m_sentGain[i]) > 1e-3f) {
            m_backend->setGain(m_voices[i], target, def.layers[i].fade);
            m_sentGain[i] = target;
        }
    }
}

std::vector<MusicDirector::Event> MusicDirector::takeEvents() {
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

} // namespace kke
