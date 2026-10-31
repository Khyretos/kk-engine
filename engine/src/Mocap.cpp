#include "kke/Mocap.h"

#include "kke/CookedFile.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace kke {

namespace {

constexpr float kDegToRad = 0.01745329252f;
constexpr size_t kMaxValues = 16u * 1024u * 1024u; // numbers in one file's motion: 64 MB of floats

// Words, braces and colons, with line numbers for the error messages.
struct Lexer {
    std::string_view text;
    size_t pos = 0;
    int line = 1;

    bool next(std::string_view& tok) {
        while (pos < text.size()) {
            const char c = text[pos];
            if (c == '\n') {
                ++line;
                ++pos;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
        if (pos >= text.size()) return false;
        const char c = text[pos];
        if (c == '{' || c == '}' || c == ':') {
            tok = text.substr(pos++, 1);
            return true;
        }
        const size_t start = pos;
        while (pos < text.size()) {
            const char d = text[pos];
            if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '{' || d == '}') break;
            ++pos;
        }
        // "Frames:" is the word and then a colon, but a colon inside a word
        // stays (Mixamo's "mixamorig:Hips").
        if (pos - start > 1 && text[pos - 1] == ':') --pos;
        tok = text.substr(start, pos - start);
        return true;
    }
    std::string_view peek() {
        const size_t p = pos;
        const int l = line;
        std::string_view t;
        if (!next(t)) t = {};
        pos = p;
        line = l;
        return t;
    }
};

bool toFloat(std::string_view tok, float& out) {
    double v = 0.0;
    const auto r = std::from_chars(tok.data(), tok.data() + tok.size(), v); // no locale: a decimal point is a point
    if (r.ec != std::errc() || r.ptr != tok.data() + tok.size() || !std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

bool channelFromName(std::string_view n, BvhChannel& out) {
    static const std::pair<const char*, BvhChannel> table[] = {
        { "Xposition", BvhChannel::Xposition }, { "Yposition", BvhChannel::Yposition }, { "Zposition", BvhChannel::Zposition },
        { "Xrotation", BvhChannel::Xrotation }, { "Yrotation", BvhChannel::Yrotation }, { "Zrotation", BvhChannel::Zrotation },
    };
    for (const auto& [name, ch] : table)
        if (n == name) {
            out = ch;
            return true;
        }
    return false;
}

struct Parser {
    Lexer lex;
    BvhMotion* out;
    std::string error;
    size_t channels = 0;

    bool fail(const std::string& why) {
        if (error.empty()) error = "line " + std::to_string(lex.line) + ": " + why;
        return false;
    }
    bool expect(std::string_view word) {
        std::string_view t;
        if (!lex.next(t)) return fail("the file ends where '" + std::string(word) + "' should be");
        if (t != word) return fail("expected '" + std::string(word) + "', found '" + std::string(t) + "'");
        return true;
    }
    bool number(float& v) {
        std::string_view t;
        if (!lex.next(t)) return fail("the file ends in the middle of a number");
        if (!toFloat(t, v)) return fail("'" + std::string(t) + "' is not a number");
        return true;
    }
    bool offset(glm::vec3& v) {
        return expect("OFFSET") && number(v.x) && number(v.y) && number(v.z);
    }

    bool joint(int parent) {
        std::string_view name;
        if (!lex.next(name) || name == "{") return fail("a joint needs a name");
        if (out->joints.size() >= kBvhMaxJoints) return fail("more than " + std::to_string(kBvhMaxJoints) + " joints");
        const int self = static_cast<int>(out->joints.size());
        BvhJoint j;
        j.name = std::string(name);
        j.parent = parent;
        out->joints.push_back(j);
        if (!expect("{")) return false;
        bool hasOffset = false, hasChannels = false;
        for (;;) {
            std::string_view t = lex.peek();
            if (t.empty()) return fail("the file ends inside joint '" + out->joints[static_cast<size_t>(self)].name + "'");
            if (t == "}") {
                std::string_view skip;
                lex.next(skip);
                break;
            }
            if (t == "OFFSET") {
                if (!offset(out->joints[static_cast<size_t>(self)].offset)) return false;
                hasOffset = true;
            } else if (t == "CHANNELS") {
                std::string_view skip;
                lex.next(skip);
                float n = 0.0f;
                if (!number(n)) return false;
                if (n < 0.0f || n > 6.0f || n != std::floor(n)) return fail("a joint has 0 to 6 channels, not " + std::to_string(n));
                BvhJoint& jj = out->joints[static_cast<size_t>(self)];
                jj.firstChannel = channels;
                for (int k = 0; k < static_cast<int>(n); ++k) {
                    std::string_view cn;
                    if (!lex.next(cn)) return fail("the file ends in a channel list");
                    BvhChannel ch;
                    if (!channelFromName(cn, ch)) return fail("unknown channel '" + std::string(cn) + "'");
                    jj.channels.push_back(ch);
                }
                channels += jj.channels.size();
                hasChannels = true;
            } else if (t == "JOINT") {
                std::string_view skip;
                lex.next(skip);
                if (!joint(self)) return false;
            } else if (t == "End") {
                std::string_view skip;
                lex.next(skip);
                if (!expect("Site") || !expect("{") || !offset(out->joints[static_cast<size_t>(self)].endSite) || !expect("}")) return false;
                out->joints[static_cast<size_t>(self)].hasEndSite = true;
            } else {
                return fail("unexpected '" + std::string(t) + "' in a joint");
            }
        }
        if (!hasOffset) return fail("joint '" + out->joints[static_cast<size_t>(self)].name + "' has no OFFSET");
        if (!hasChannels) return fail("joint '" + out->joints[static_cast<size_t>(self)].name + "' has no CHANNELS");
        return true;
    }
};

glm::vec3 turnZUp(const glm::vec3& v) { return { v.x, v.z, -v.y }; } // rotation about X by -90 degrees

} // namespace

bool parseBvh(const std::string& text, BvhMotion& out, std::string* error) {
    BvhMotion motion;
    Parser p;
    p.lex.text = text;
    p.out = &motion;
    auto done = [&](bool ok) {
        if (!ok && error) *error = p.error;
        if (ok) out = std::move(motion);
        return ok;
    };
    if (!p.expect("HIERARCHY")) return done(false);
    while (p.lex.peek() == "ROOT") {
        std::string_view skip;
        p.lex.next(skip);
        if (!p.joint(-1)) return done(false);
    }
    if (motion.joints.empty()) return done(p.fail("no ROOT joint"));
    motion.channelCount = p.channels;
    if (motion.channelCount == 0) return done(p.fail("the skeleton has no channels"));
    if (!p.expect("MOTION")) return done(false);
    float frames = 0.0f, frameTime = 0.0f;
    if (!p.expect("Frames") || !p.expect(":") || !p.number(frames)) return done(false);
    if (frames < 0.0f || frames != std::floor(frames) || frames > static_cast<float>(kBvhMaxFrames))
        return done(p.fail("a clip has 0 to " + std::to_string(kBvhMaxFrames) + " frames, not " + std::to_string(frames)));
    if (!p.expect("Frame") || !p.expect("Time") || !p.expect(":") || !p.number(frameTime)) return done(false);
    if (!(frameTime > 0.0f) || frameTime > 10.0f) return done(p.fail("the frame time must be above 0 and at most 10 seconds"));
    motion.frameCount = static_cast<size_t>(frames);
    motion.frameTime = frameTime;
    if (motion.frameCount * motion.channelCount > kMaxValues) return done(p.fail("more than " + std::to_string(kMaxValues) + " numbers of motion"));
    motion.values.reserve(motion.frameCount * motion.channelCount);
    for (size_t f = 0; f < motion.frameCount; ++f) {
        for (size_t c = 0; c < motion.channelCount; ++c) {
            float v = 0.0f;
            std::string_view t;
            if (!p.lex.next(t)) return done(p.fail("the file ends in frame " + std::to_string(f) + ": " + std::to_string(c) + " of " + std::to_string(motion.channelCount) + " numbers"));
            if (!toFloat(t, v)) return done(p.fail("'" + std::string(t) + "' is not a number (frame " + std::to_string(f) + ")"));
            motion.values.push_back(v);
        }
    }
    std::string_view extra;
    if (p.lex.next(extra)) return done(p.fail("more numbers than " + std::to_string(motion.frameCount) + " frames of " + std::to_string(motion.channelCount)));
    return done(true);
}

ModelData bvhToModel(const BvhMotion& motion, const BvhImportOptions& options) {
    ModelData model;
    const size_t nj = motion.joints.size();
    // The rest pose in the file's units, to see how big the skeleton is.
    std::vector<glm::vec3> rest(nj);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (size_t i = 0; i < nj; ++i) {
        const BvhJoint& j = motion.joints[i];
        rest[i] = j.offset + (j.parent >= 0 ? rest[static_cast<size_t>(j.parent)] : glm::vec3(0.0f));
        lo = glm::min(lo, rest[i]);
        hi = glm::max(hi, rest[i]);
    }
    const glm::vec3 size = nj ? hi - lo : glm::vec3(0.0f);
    const float extent = std::max({ size.x, size.y, size.z });
    const float scale = options.scale > 0.0f ? options.scale : (extent > 3.0f ? 0.01f : 1.0f);

    const glm::quat turn = options.zUp ? glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1, 0, 0)) : glm::quat(1, 0, 0, 0);
    auto basis = [&](const glm::vec3& v) { return options.zUp ? turnZUp(v) : v; };

    std::vector<glm::mat4> modelRest(nj);
    for (size_t i = 0; i < nj; ++i) {
        const BvhJoint& j = motion.joints[i];
        ModelBone b;
        b.name = j.name;
        b.parent = j.parent;
        b.localRest = glm::translate(glm::mat4(1.0f), basis(j.offset * scale));
        modelRest[i] = (j.parent >= 0 ? modelRest[static_cast<size_t>(j.parent)] : glm::mat4(1.0f)) * b.localRest;
        b.inverseBind = glm::inverse(modelRest[i]);
        model.bones.push_back(b);
    }

    ModelAnimation clip;
    clip.name = !options.name.empty() ? options.name : "mocap";
    clip.sampleRate = motion.rate();
    clip.duration = motion.frameCount > 1 ? static_cast<float>(motion.frameCount - 1) * motion.frameTime : 0.0f;
    clip.frames.resize(motion.frameCount);
    for (size_t f = 0; f < motion.frameCount; ++f) {
        std::vector<glm::mat4>& locals = clip.frames[f];
        locals.resize(nj);
        for (size_t i = 0; i < nj; ++i) {
            const BvhJoint& j = motion.joints[i];
            glm::vec3 t = j.offset;
            glm::quat q(1, 0, 0, 0);
            for (size_t c = 0; c < j.channels.size(); ++c) {
                const float v = motion.at(f, j.firstChannel + c);
                switch (j.channels[c]) {
                case BvhChannel::Xposition: t.x += v; break;
                case BvhChannel::Yposition: t.y += v; break;
                case BvhChannel::Zposition: t.z += v; break;
                // listed order: the first rotation listed is the outermost, M = R1 * R2 * R3
                case BvhChannel::Xrotation: q = q * glm::angleAxis(v * kDegToRad, glm::vec3(1, 0, 0)); break;
                case BvhChannel::Yrotation: q = q * glm::angleAxis(v * kDegToRad, glm::vec3(0, 1, 0)); break;
                case BvhChannel::Zrotation: q = q * glm::angleAxis(v * kDegToRad, glm::vec3(0, 0, 1)); break;
                }
            }
            if (options.zUp) { // every local transform turned the same way: L' = C L C^-1
                t = turn * t;
                q = turn * q * glm::conjugate(turn);
            }
            locals[i] = glm::translate(glm::mat4(1.0f), t * scale) * glm::mat4_cast(glm::normalize(q));
        }
    }
    model.animations.push_back(std::move(clip));
    return model;
}

ModelData loadBvh(const std::string& path, const BvhImportOptions& options) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!cooked::readAssetFile(path, bytes, &error)) throw std::runtime_error("loadBvh: " + error);
    BvhMotion motion;
    if (!parseBvh(std::string(bytes.begin(), bytes.end()), motion, &error)) throw std::runtime_error("loadBvh: '" + path + "': " + error);
    BvhImportOptions o = options;
    if (o.name.empty()) o.name = std::filesystem::path(path).stem().string();
    ModelData model = bvhToModel(motion, o);
    model.sourcePath = path;
    return model;
}

// ---- clean-up -----------------------------------------------------------------------

namespace {

float angleBetween(const glm::quat& a, const glm::quat& b) {
    const glm::quat d = a * glm::conjugate(b);
    return 2.0f * std::atan2(glm::length(glm::vec3(d.x, d.y, d.z)), std::abs(d.w)) * 57.29577951f;
}

} // namespace

std::vector<Pose> cleanMotion(const std::vector<Pose>& input, float rate, const MotionCleanOptions& o, MotionCleanReport* report) {
    MotionCleanReport rep;
    rep.framesBefore = input.size();
    rep.sampleRate = rate;
    std::vector<Pose> f = input;
    if (rate <= 0.0f || f.empty()) {
        rep.framesAfter = f.size();
        if (report) *report = rep;
        return f;
    }

    // trim
    const size_t cutStart = static_cast<size_t>(std::max(0.0f, std::round(o.trimStart * rate)));
    const size_t cutEnd = static_cast<size_t>(std::max(0.0f, std::round(o.trimEnd * rate)));
    if (cutStart + cutEnd < f.size()) {
        f.erase(f.begin() + static_cast<std::ptrdiff_t>(f.size() - cutEnd), f.end());
        f.erase(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(cutStart));
    }
    const size_t bones = f[0].size();

    // flips: the same rotation with the other sign
    if (o.fixFlips)
        for (size_t b = 0; b < bones; ++b)
            for (size_t i = 1; i < f.size(); ++i)
                if (glm::dot(f[i - 1][b].r, f[i][b].r) < 0.0f) f[i][b].r = -f[i][b].r;

    // spikes: out and straight back
    if ((o.spikeDegrees > 0.0f || o.spikeMetres > 0.0f) && f.size() > 2) {
        for (size_t b = 0; b < bones; ++b) {
            for (size_t i = 1; i + 1 < f.size(); ++i) {
                BoneTRS& cur = f[i][b];
                const BoneTRS& p = f[i - 1][b];
                const BoneTRS& n = f[i + 1][b];
                if (o.spikeDegrees > 0.0f) {
                    const float away = std::min(angleBetween(p.r, cur.r), angleBetween(cur.r, n.r));
                    if (away > o.spikeDegrees && angleBetween(p.r, n.r) < o.spikeDegrees * 0.5f) {
                        cur.r = glm::slerp(p.r, n.r, 0.5f);
                        ++rep.spikesFixed;
                    }
                }
                if (o.spikeMetres > 0.0f) {
                    const float away = std::min(glm::length(cur.t - p.t), glm::length(cur.t - n.t));
                    if (away > o.spikeMetres && glm::length(p.t - n.t) < o.spikeMetres * 0.5f) {
                        cur.t = 0.5f * (p.t + n.t);
                        ++rep.spikesFixed;
                    }
                }
            }
        }
    }

    // smoothing: a Gaussian over the neighbouring frames
    if (o.smoothSeconds > 0.0f && f.size() > 2) {
        const float sigma = std::max(o.smoothSeconds * rate, 0.25f); // in frames
        const int radius = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
        std::vector<float> weight(static_cast<size_t>(radius) + 1);
        for (int k = 0; k <= radius; ++k) weight[static_cast<size_t>(k)] = std::exp(-0.5f * static_cast<float>(k * k) / (sigma * sigma));
        const std::vector<Pose> src = f;
        const int n = static_cast<int>(f.size());
        for (size_t b = 0; b < bones; ++b) {
            for (int i = 0; i < n; ++i) {
                glm::vec3 t(0.0f), s(0.0f);
                glm::vec4 q(0.0f);
                float total = 0.0f;
                const glm::quat ref = src[static_cast<size_t>(i)][b].r;
                for (int k = -radius; k <= radius; ++k) {
                    const int j = i + k;
                    if (j < 0 || j >= n) continue;
                    const float w = weight[static_cast<size_t>(std::abs(k))];
                    const BoneTRS& x = src[static_cast<size_t>(j)][b];
                    t += w * x.t;
                    s += w * x.s;
                    const glm::quat r = glm::dot(ref, x.r) < 0.0f ? -x.r : x.r;
                    q += w * glm::vec4(r.x, r.y, r.z, r.w);
                    total += w;
                }
                BoneTRS& dst = f[static_cast<size_t>(i)][b];
                dst.t = t / total;
                dst.s = s / total;
                const glm::vec4 qn = q / glm::length(q);
                dst.r = glm::quat(qn.w, qn.x, qn.y, qn.z);
            }
        }
    }

    // resample
    float outRate = rate;
    if (o.resampleRate > 0.0f && f.size() > 1 && std::abs(o.resampleRate - rate) > 1e-4f) {
        const float duration = static_cast<float>(f.size() - 1) / rate;
        const size_t count = static_cast<size_t>(std::floor(duration * o.resampleRate + 1e-3f)) + 1;
        std::vector<Pose> r(count);
        for (size_t i = 0; i < count; ++i) {
            const float pos = std::min(static_cast<float>(i) / o.resampleRate * rate, static_cast<float>(f.size() - 1));
            const size_t a = std::min(static_cast<size_t>(pos), f.size() - 1);
            const size_t c = std::min(a + 1, f.size() - 1);
            blendPoses(f[a], f[c], pos - static_cast<float>(a), r[i]);
        }
        f = std::move(r);
        outRate = o.resampleRate;
    }

    // in place: the root's horizontal travel goes
    if (o.inPlaceBone >= 0 && static_cast<size_t>(o.inPlaceBone) < bones && !f.empty()) {
        const size_t b = static_cast<size_t>(o.inPlaceBone);
        const glm::vec3 first = f.front()[b].t;
        rep.travel = glm::vec3(f.back()[b].t.x - first.x, 0.0f, f.back()[b].t.z - first.z);
        for (Pose& pose : f) {
            pose[b].t.x = first.x;
            pose[b].t.z = first.z;
        }
    }

    // loop: the end crossfades into the start; what is left starts where the end leaves off
    if (o.loopBlendSeconds > 0.0f) {
        const size_t k = static_cast<size_t>(std::round(o.loopBlendSeconds * outRate));
        if (k >= 1 && f.size() > 2 * k) {
            std::vector<Pose> loop(f.begin() + static_cast<std::ptrdiff_t>(k), f.end());
            const size_t n = f.size();
            for (size_t j = 0; j < k; ++j) {
                const float w = static_cast<float>(j + 1) / static_cast<float>(k + 1);
                blendPoses(f[n - k + j], f[j], w, loop[n - 2 * k + j]);
            }
            f = std::move(loop);
        }
    }

    rep.framesAfter = f.size();
    rep.sampleRate = outRate;
    if (report) *report = rep;
    return f;
}

ModelAnimation cleanAnimation(const ModelAnimation& clip, const MotionCleanOptions& options, MotionCleanReport* report) {
    std::vector<Pose> frames(clip.frames.size());
    for (size_t i = 0; i < clip.frames.size(); ++i) {
        frames[i].resize(clip.frames[i].size());
        for (size_t b = 0; b < clip.frames[i].size(); ++b) {
            BoneTRS tr;
            glm::vec3 skew;
            glm::vec4 persp;
            if (!glm::decompose(clip.frames[i][b], tr.s, tr.r, tr.t, skew, persp)) tr = BoneTRS{};
            tr.r = glm::normalize(tr.r);
            frames[i][b] = tr;
        }
    }
    const float rate = clip.sampleRate > 0.0f ? clip.sampleRate : 30.0f;
    MotionCleanReport rep;
    std::vector<Pose> cleaned = cleanMotion(frames, rate, options, &rep);
    ModelAnimation out;
    out.name = clip.name;
    out.sampleRate = rep.sampleRate;
    out.frames.resize(cleaned.size());
    for (size_t i = 0; i < cleaned.size(); ++i) poseToLocals(cleaned[i], out.frames[i]);
    // A clip made to loop plays its last frame into its first: one more frame time.
    const size_t span = options.loopBlendSeconds > 0.0f && rep.framesAfter < rep.framesBefore ? cleaned.size() : (cleaned.empty() ? 0 : cleaned.size() - 1);
    out.duration = static_cast<float>(span) / rep.sampleRate;
    if (report) *report = rep;
    return out;
}

} // namespace kke
