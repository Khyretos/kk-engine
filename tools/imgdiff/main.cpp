// kke_imgdiff: compares screenshots with reference pictures (ENG-F69,
// docs/TEST_HARNESS.md). Exit code 0 when every pair matches.
//
//   kke_imgdiff REFERENCE ACTUAL               one pair
//   kke_imgdiff REFDIR ACTUALDIR               every image in REFDIR against
//                                              the same name in ACTUALDIR
//   --tolerance N    channel difference (0-255) still counted as equal (default 8)
//   --max-diff F     fraction of pixels allowed to differ (default 0.002)
//   --diff DIR       write <name>.diff.png for each pair that fails
//
// A missing actual picture fails; a picture only in ACTUALDIR is ignored.

#include "kke/TestHarness.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

bool load(const fs::path& p, Image& out) {
    int channels = 0;
    unsigned char* data = stbi_load(p.string().c_str(), &out.w, &out.h, &channels, 4);
    if (!data) return false;
    out.rgba.assign(data, data + static_cast<size_t>(out.w) * out.h * 4);
    stbi_image_free(data);
    return true;
}

bool isImage(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

} // namespace

int main(int argc, char** argv) {
    int tolerance = 8;
    double maxDiff = 0.002;
    fs::path diffDir;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tolerance" && i + 1 < argc) tolerance = std::atoi(argv[++i]);
        else if (a == "--max-diff" && i + 1 < argc) maxDiff = std::atof(argv[++i]);
        else if (a == "--diff" && i + 1 < argc) diffDir = argv[++i];
        else if (a == "-h" || a == "--help") {
            std::puts("usage: kke_imgdiff [--tolerance N] [--max-diff F] [--diff DIR] REFERENCE ACTUAL\n"
                      "       (two pictures, or two folders: every picture in REFERENCE against the same name in ACTUAL)");
            return 0;
        } else paths.push_back(a);
    }
    if (paths.size() != 2) {
        std::fprintf(stderr, "kke_imgdiff: give a reference and an actual picture (or folder); --help\n");
        return 2;
    }
    std::vector<std::pair<fs::path, fs::path>> pairs;
    if (fs::is_directory(paths[0])) {
        for (const auto& entry : fs::directory_iterator(paths[0]))
            if (entry.is_regular_file() && isImage(entry.path())) pairs.emplace_back(entry.path(), fs::path(paths[1]) / entry.path().filename());
        if (pairs.empty()) {
            std::fprintf(stderr, "kke_imgdiff: no pictures in %s\n", paths[0].c_str());
            return 2;
        }
    } else {
        pairs.emplace_back(paths[0], paths[1]);
    }
    int failed = 0;
    for (const auto& [refPath, actPath] : pairs) {
        Image ref, act;
        if (!load(refPath, ref)) {
            std::printf("FAIL %s: can't read the reference\n", refPath.string().c_str());
            ++failed;
            continue;
        }
        if (!load(actPath, act)) {
            std::printf("FAIL %s: no picture at %s\n", refPath.filename().string().c_str(), actPath.string().c_str());
            ++failed;
            continue;
        }
        const kke::harness::ImageDiff d = kke::harness::compareImages(ref.rgba, static_cast<uint32_t>(ref.w), static_cast<uint32_t>(ref.h), act.rgba,
                                                                      static_cast<uint32_t>(act.w), static_cast<uint32_t>(act.h), tolerance);
        const bool ok = kke::harness::imagesMatch(d, maxDiff);
        if (!d.sameSize) {
            std::printf("FAIL %s: %dx%d, expected %dx%d\n", refPath.filename().string().c_str(), act.w, act.h, ref.w, ref.h);
        } else {
            std::printf("%s %s: %.3f%% of pixels differ (allowed %.3f%%), mean %.2f, max %d, PSNR %.1f dB\n", ok ? "ok  " : "FAIL",
                        refPath.filename().string().c_str(), d.differingFraction * 100.0, maxDiff * 100.0, d.meanAbs, d.maxAbs, d.psnr);
        }
        if (!ok) {
            ++failed;
            if (!diffDir.empty() && d.sameSize) {
                std::error_code ec;
                fs::create_directories(diffDir, ec);
                const std::vector<uint8_t> pic = kke::harness::diffImage(ref.rgba, act.rgba, static_cast<uint32_t>(ref.w), static_cast<uint32_t>(ref.h), tolerance);
                const fs::path out = diffDir / (refPath.stem().string() + ".diff.png");
                if (stbi_write_png(out.string().c_str(), ref.w, ref.h, 4, pic.data(), ref.w * 4)) std::printf("     diff: %s\n", out.string().c_str());
            }
        }
    }
    std::printf("%s: %zu picture(s), %d failed\n", failed ? "FAILED" : "OK", pairs.size(), failed);
    return failed ? 1 : 0;
}
