// kke_bench: the engine's headless CPU benchmark suite (issue #18,
// docs/BENCHMARKS.md). No window, no GPU: each case builds a fixed,
// seeded workload from the engine's own pure-logic code (the same code
// the unit tests cover), times it, and the whole run is written as one
// kke::BenchmarkReport (.json + .txt).
//
//   kke_bench                     every case, default repeats
//   kke_bench --quick             fewer repeats (smoke run, CI on PRs)
//   kke_bench --filter rigid      only cases whose name contains "rigid"
//   kke_bench --list              print the cases and exit
//   kke_bench --out DIR           report directory (default: $KKE_BENCH_DIR, else "benchmark")
//
// Every case runs single-threaded on purpose: the numbers track the
// code, not the runner's core count, so a change in them means the code
// got faster or slower. (Thread scaling is tools/run_physics_benchmarks.cmake's job.)
//
// Each case reports the median and p95 of its timed repeats in ms, plus a
// checksum of what it computed. The checksum keeps the optimizer from
// deleting the work, and a changed checksum between two runs of the same
// commit means the workload isn't deterministic, which is itself a bug.

#include "kke/AudioMixer.h"
#include "kke/BenchmarkReport.h"
#include "kke/FracturePattern.h"
#include "kke/ImpactSynth.h"
#include "kke/ParticleFluid.h"
#include "kke/ResourceGovernor.h"
#include "kke/VoronoiFracture.h"
#include "kke/VoxelTets.h"
#include "kke/net/BitStream.h"
#if KKE_ENABLE_JOLT
#include "kke/RigidWorld.h"
#endif
#if KKE_ENABLE_LUA
#include "kke/ScriptVM.h"
#endif

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// A benchmark case: `setup` builds the workload (not timed) and returns
// the timed body. One call of the body is one sample.
struct Case {
    const char* name;
    const char* what; // one line, for the report and --list
    int repeats;      // timed samples in a full run (--quick divides by 4)
    std::function<std::function<double()>()> setup;
};

// Deterministic random numbers (same crates, same seeds on every machine).
struct Lcg {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / 16777216.0f;
    }
};

kke::TetMeshData voxelBox(glm::vec3 size, float cell) {
    std::vector<glm::vec3> p;
    for (int i = 0; i < 8; ++i) p.push_back(glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) * size - size * 0.5f);
    const std::vector<uint32_t> idx = { 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
    return kke::voxelizeToTets(p, idx, cell, 1000000).mesh;
}

std::vector<Case> makeCases() {
    std::vector<Case> cases;

#if KKE_ENABLE_JOLT
    // 400 crates dropped in a heap on a floor: one sample = one 60 Hz step,
    // taken while they fall, collide and pile up (the stress test's crate
    // rain, without the renderer).
    cases.push_back({ "rigid_crates_400", "Jolt: one 60 Hz step of 400 falling, piling crates (1 thread)", 240, [] {
        auto w = std::make_shared<kke::RigidWorld>([] {
            kke::RigidWorld::Settings s;
            s.threads = 0;
            return s;
        }());
        kke::RigidWorld::BodyDesc g;
        g.motion = kke::RigidWorld::Motion::Static;
        g.halfExtents = glm::vec3(30.0f, 0.5f, 30.0f);
        g.position = glm::vec3(0.0f, -0.5f, 0.0f);
        w->add(g);
        Lcg r{ 12345u };
        for (int i = 0; i < 400; ++i) {
            kke::RigidWorld::BodyDesc d;
            d.halfExtents = glm::vec3(0.2f + 0.25f * r.next());
            d.density = 250.0f;
            d.position = glm::vec3((r.next() - 0.5f) * 6.0f, 1.0f + float(i) * 0.05f, (r.next() - 0.5f) * 6.0f);
            d.rotation = glm::angleAxis(r.next() * 6.2832f, glm::normalize(glm::vec3(r.next() - 0.5f, 1.0f, r.next() - 0.5f)));
            w->add(d);
        }
        return std::function<double()>([w] {
            w->step(1.0f / 60.0f);
            return double(w->activeBodyCount());
        });
    } });

    // Raycasts into a settled pile: what picking, audio occlusion and the
    // camera spring arm pay per query.
    cases.push_back({ "rigid_raycast_1000", "Jolt: 1000 raycasts into a settled pile of 400 crates", 200, [] {
        auto w = std::make_shared<kke::RigidWorld>([] {
            kke::RigidWorld::Settings s;
            s.threads = 0;
            return s;
        }());
        kke::RigidWorld::BodyDesc g;
        g.motion = kke::RigidWorld::Motion::Static;
        g.halfExtents = glm::vec3(30.0f, 0.5f, 30.0f);
        g.position = glm::vec3(0.0f, -0.5f, 0.0f);
        w->add(g);
        Lcg r{ 777u };
        for (int i = 0; i < 400; ++i) {
            kke::RigidWorld::BodyDesc d;
            d.halfExtents = glm::vec3(0.3f);
            d.position = glm::vec3((r.next() - 0.5f) * 8.0f, 0.5f + float(i) * 0.02f, (r.next() - 0.5f) * 8.0f);
            w->add(d);
        }
        for (int i = 0; i < 300; ++i) w->step(1.0f / 60.0f);
        return std::function<double()>([w] {
            Lcg q{ 99u };
            double sum = 0.0;
            for (int i = 0; i < 1000; ++i) {
                const glm::vec3 from((q.next() - 0.5f) * 12.0f, 8.0f, (q.next() - 0.5f) * 12.0f);
                const glm::vec3 dir = glm::normalize(glm::vec3((q.next() - 0.5f) * 0.6f, -1.0f, (q.next() - 0.5f) * 0.6f));
                const auto hit = w->raycast(from, dir, 50.0f);
                if (hit.hit) sum += hit.distance;
            }
            return sum;
        });
    } });
#endif

    // Pre-fracturing a 1 m cube (10 cm voxel tets) into Voronoi pieces:
    // what a breakable prop costs when it's spawned.
    cases.push_back({ "fracture_bake_cube", "Voxelize a 1 m cube at 10 cm and bake a Voronoi fracture (~30 pieces)", 40, [] {
        return std::function<double()>([] {
            const kke::TetMeshData m = voxelBox(glm::vec3(1.0f), 0.1f);
            kke::FractureSeedOptions o;
            o.pattern = kke::FracturePattern::Voronoi;
            o.chunkSize = 0.3f;
            o.seed = 42;
            const kke::BakedFracture b = kke::bakeFracture(m, o);
            return double(b.pieces) * 1000.0 + double(m.tets.size());
        });
    } });

    // 2000 fluid particles (the melt/lava demos' budget) sloshing in a box.
    cases.push_back({ "particle_fluid_2000", "Position Based Fluids: one 60 Hz step of 2000 particles in a box", 120, [] {
        kke::ParticleFluid::Params p;
        p.radius = 0.04f;
        p.boundsMin = glm::vec3(-0.6f, -1.0f, -0.6f);
        p.boundsMax = glm::vec3(0.6f, 10.0f, 0.6f);
        auto f = std::make_shared<kke::ParticleFluid>(p, 2000);
        for (int x = 0; x < 10; ++x)
            for (int y = 0; y < 20; ++y)
                for (int z = 0; z < 10; ++z) f->add(glm::vec3(x - 4.5f, y + 2.0f, z - 4.5f) * 0.08f, glm::vec3(0.0f), 20.0f);
        for (int i = 0; i < 30; ++i) f->step(1.0f / 60.0f); // past the first free fall
        return std::function<double()>([f] {
            f->step(1.0f / 60.0f);
            return double(f->positions()[0].y);
        });
    } });

    // The mixer at its voice budget: 32 spatial voices, reverb on. One
    // sample = 10 ms of output, so a median under 10 ms is real time and
    // (10 / median) is the headroom.
    cases.push_back({ "audio_mix_32_voices", "Audio mixer: 10 ms of stereo output with 32 spatial voices and room reverb", 400, [] {
        auto m = std::make_shared<kke::AudioMixer>(48000, 32);
        m->setRoom(1.2f, 0.4f, 0.3f, 0.02f);
        const kke::AudioMaterialTable table;
        Lcg r{ 5u };
        for (int i = 0; i < 32; ++i) {
            kke::ImpactParams ip;
            ip.intensity = 0.5f + 0.5f * r.next();
            ip.seed = uint32_t(i + 1);
            auto buf = std::make_shared<kke::SoundBuffer>(kke::synthesizeImpact(table.get(uint32_t(1 + i % 7)), ip));
            kke::VoiceDesc v;
            v.sound = buf;
            v.loop = true;
            v.position = glm::vec3((r.next() - 0.5f) * 20.0f, r.next() * 3.0f, (r.next() - 0.5f) * 20.0f);
            m->play(v);
        }
        auto out = std::make_shared<std::vector<float>>(480 * 2);
        return std::function<double()>([m, out] {
            m->mix(out->data(), 480);
            return double((*out)[0]);
        });
    } });

    // Synthesizing impact sounds (modal synthesis) for every material:
    // the first hit of a new material/intensity pays this, later hits
    // come from the cache.
    cases.push_back({ "impact_synth_8_materials", "Modal synthesis: one impact sound for each of the 8 default materials", 60, [] {
        auto table = std::make_shared<kke::AudioMaterialTable>();
        return std::function<double()>([table] {
            double sum = 0.0;
            for (uint32_t id = 0; id < 8; ++id) {
                kke::ImpactParams ip;
                ip.intensity = 0.8f;
                ip.seed = id + 1;
                sum += double(kke::synthesizeImpact(table->get(id), ip).samples.size());
            }
            return sum;
        });
    } });

#if KKE_ENABLE_LUA
    // Gameplay scripting overhead: 50 scripts' Think hooks doing a little
    // math, run once per frame.
    cases.push_back({ "lua_think_50_hooks", "Lua: one Think hook call fanned out to 50 scripted handlers", 400, [] {
        auto vm = std::make_shared<kke::ScriptVM>();
        vm->runString(R"(
            total = 0
            for i = 1, 50 do
                hook.Add("Think", "bench" .. i, function(dt)
                    local x = 0
                    for k = 1, 20 do x = x + math.sin(k * dt) end
                    total = total + x
                end)
            end
        )",
                      "bench");
        return std::function<double()>([vm] {
            vm->callHook("Think", 1.0 / 60.0);
            return double(vm->errors().size());
        });
    } });
#endif

    // A network snapshot of 256 bodies (position, rotation, velocity),
    // written and read back: the host pays the write per client per tick.
    cases.push_back({ "net_snapshot_256_bodies", "Net protocol: bit-pack and unpack a 256-body snapshot", 400, [] {
        struct Body { glm::vec3 p, v; glm::quat q; };
        auto bodies = std::make_shared<std::vector<Body>>();
        Lcg r{ 31u };
        for (int i = 0; i < 256; ++i)
            bodies->push_back({ glm::vec3(r.next(), r.next(), r.next()) * 200.0f - 100.0f, glm::vec3(r.next(), r.next(), r.next()) * 10.0f - 5.0f,
                                glm::normalize(glm::quat(r.next() - 0.5f, r.next() - 0.5f, r.next() - 0.5f, r.next() - 0.5f)) });
        auto buf = std::make_shared<std::vector<uint8_t>>();
        return std::function<double()>([bodies, buf] {
            buf->clear();
            {
                kke::net::WriteStream w(*buf);
                for (Body b : *bodies) {
                    w.vec3(b.p, 4096.0f, 1.0f / 512.0f);
                    w.vec3(b.v, 64.0f, 1.0f / 256.0f);
                    w.quat(b.q);
                }
            }
            kke::net::ReadStream rd(buf->data(), buf->size());
            double sum = 0.0;
            for (size_t i = 0; i < bodies->size(); ++i) {
                Body b{};
                rd.vec3(b.p, 4096.0f, 1.0f / 512.0f);
                rd.vec3(b.v, 64.0f, 1.0f / 256.0f);
                rd.quat(b.q);
                sum += double(b.p.x);
            }
            return sum + double(buf->size());
        });
    } });

    return cases;
}

double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

} // namespace

int main(int argc, char** argv) {
    bool quick = false, list = false;
    std::string filter, outDir;
    if (const char* e = std::getenv("KKE_BENCH_DIR"); e && *e) outDir = e;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--quick") quick = true;
        else if (a == "--list") list = true;
        else if (a == "--filter" && i + 1 < argc) filter = argv[++i];
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else {
            std::fprintf(stderr, "usage: kke_bench [--quick] [--list] [--filter TEXT] [--out DIR]\n");
            return 2;
        }
    }
    if (outDir.empty()) outDir = "benchmark";

    const std::vector<Case> cases = makeCases();
    if (list) {
        for (const Case& c : cases) std::printf("%-26s %s\n", c.name, c.what);
        return 0;
    }

    kke::BenchmarkReport report;
    report.name = "suite";
    report.system = kke::collectSystemInfo();
    report.config = {
        { "suite", "kke_bench (benchmarks/kke_bench/main.cpp): headless CPU cases, single-threaded, seeded workloads" },
        { "mode", quick ? "quick (a quarter of the repeats)" : "full" },
        { "filter", filter.empty() ? "(all)" : filter },
        { "usable_cores", std::to_string(kke::usableCpuCount()) },
    };
    report.sampleColumns = { "case", "median_ms", "p95_ms", "min_ms", "repeats", "checksum" };
    report.notes.push_back("One row per case in `samples`; the case is its index in the results list. Lower ms is better.");

    int ran = 0;
    const auto start = Clock::now();
    for (const Case& c : cases) {
        if (!filter.empty() && std::strstr(c.name, filter.c_str()) == nullptr) continue;
        const int repeats = std::max(3, quick ? c.repeats / 4 : c.repeats);
        std::function<double()> body = c.setup();
        body(); // warm-up: first-touch allocations, caches
        std::vector<double> times;
        times.reserve(size_t(repeats));
        double checksum = 0.0;
        for (int i = 0; i < repeats; ++i) {
            const auto t0 = Clock::now();
            checksum += body();
            times.push_back(ms(Clock::now() - t0));
        }
        const double median = kke::percentile(times, 50.0), p95 = kke::percentile(times, 95.0);
        const double best = *std::min_element(times.begin(), times.end());
        report.results.emplace_back(std::string(c.name) + "_ms", median);
        report.results.emplace_back(std::string(c.name) + "_p95_ms", p95);
        report.samples.push_back({ double(ran), median, p95, best, double(repeats), checksum });
        report.notes.push_back(std::string("case ") + std::to_string(ran) + " = " + c.name + ": " + c.what);
        std::printf("%-26s median %9.4f ms   p95 %9.4f ms   min %9.4f ms   (%d runs)\n", c.name, median, p95, best, repeats);
        std::fflush(stdout);
        ++ran;
    }
    if (ran == 0) {
        std::fprintf(stderr, "kke_bench: no case matches '%s' (see --list)\n", filter.c_str());
        return 2;
    }
    report.results.emplace_back("total_wall_s", ms(Clock::now() - start) / 1000.0);
    report.results.emplace_back("peak_rss_mb", kke::peakResidentMemoryMb());

    const std::string path = report.writeFiles(outDir, kke::timestampForFileName(), kke::hostNameForFileName());
    if (path.empty()) {
        std::fprintf(stderr, "kke_bench: could not write the report to '%s'\n", outDir.c_str());
        return 1;
    }
    std::printf("report: %s.json and .txt\n", path.c_str());
    return 0;
}
