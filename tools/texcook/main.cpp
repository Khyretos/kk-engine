// kke_texcook: compresses textures to BC7 in KTX2 files next to them
// (ENG-F60, docs/ASSETS.md "Compressed textures"). kke::Texture loads
// foo.ktx2 instead of foo.png when it is there: a quarter of the memory
// and no mips to make at load.
//
//   kke_texcook PATH...             every image under each folder (or the files named)
//   kke_texcook --linear PATH...    as data (normal, roughness, metallic maps), not colour
//   kke_texcook --colour PATH...    as colour, whatever the names say
//   kke_texcook --force PATH...     again, even where the .ktx2 is newer
//   kke_texcook --threads N ...     cores to use (default: all)
//
// Without --linear or --colour the name decides: "normal", "nrm",
// "rough", "metal" or "_orm" mean data, anything else colour. A wrong
// guess costs nothing but the twin: kke::Texture checks the colour space
// and loads the source image instead, with a warning. Exit code 0 = done.

#include "kke/CookedFile.h"
#include "kke/Ktx2.h"
#include "kke/TextureCompress.h"

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::set<std::string> kImages = { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isImage(const fs::path& p) { return kImages.count(lower(p.extension().string())) != 0; }

bool looksLikeData(const fs::path& p) {
    const std::string n = lower(p.stem().string());
    for (const char* word : { "normal", "nrm", "rough", "metal", "_orm" })
        if (n.find(word) != std::string::npos) return true;
    return false;
}

enum class Space { ByName, Colour, Linear };

struct Options {
    Space space = Space::ByName;
    bool force = false;
    unsigned threads = 0;
};

int cookOne(const fs::path& src, const Options& o, size_t& before, size_t& after) {
    fs::path out = src;
    out.replace_extension(".ktx2");
    std::error_code ec;
    if (!o.force && fs::exists(out, ec) && fs::last_write_time(out, ec) >= fs::last_write_time(src, ec)) return 0; // up to date
    std::vector<uint8_t> bytes;
    std::string error;
    if (!kke::cooked::readAssetFile(src.string(), bytes, &error)) {
        std::fprintf(stderr, "kke_texcook: %s: %s\n", src.string().c_str(), error.c_str());
        return 1;
    }
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!pixels) {
        std::fprintf(stderr, "kke_texcook: %s: %s\n", src.string().c_str(), stbi_failure_reason() ? stbi_failure_reason() : "can't decode");
        return 1;
    }
    const bool srgb = o.space == Space::Colour || (o.space == Space::ByName && !looksLikeData(src));
    const auto start = std::chrono::steady_clock::now();
    const kke::ktx2::Image image = kke::cookBc7(pixels, uint32_t(w), uint32_t(h), srgb, o.threads);
    stbi_image_free(pixels);
    const std::vector<uint8_t> file = kke::ktx2::write(image);
    fs::path tmp = out;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        if (!f) {
            std::fprintf(stderr, "kke_texcook: can't write %s\n", tmp.string().c_str());
            return 1;
        }
    }
    fs::rename(tmp, out, ec);
    if (ec) {
        std::fprintf(stderr, "kke_texcook: can't write %s: %s\n", out.string().c_str(), ec.message().c_str());
        return 1;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    // What the GPU would hold: RGBA8 with a full mip chain is 4/3 of level 0.
    const size_t rgba = size_t(w) * h * 4 * 4 / 3;
    before += rgba;
    after += file.size();
    std::printf("%s  %dx%d %s  %.1f MB -> %.1f MB  %.0f ms\n", out.string().c_str(), w, h, srgb ? "colour" : "data", rgba / 1048576.0,
                file.size() / 1048576.0, ms);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    std::vector<fs::path> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--linear") o.space = Space::Linear;
        else if (a == "--colour" || a == "--color") o.space = Space::Colour;
        else if (a == "--force") o.force = true;
        else if (a == "--threads" && i + 1 < argc) o.threads = unsigned(std::max(1, std::atoi(argv[++i])));
        else if (a == "-h" || a == "--help") {
            std::printf("usage: kke_texcook [--linear|--colour] [--force] [--threads N] PATH...\n"
                        "Writes a BC7 .ktx2 next to each image (kke::Texture loads it instead).\n");
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "kke_texcook: unknown option %s (try --help)\n", a.c_str());
            return 2;
        } else {
            paths.emplace_back(a);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr, "usage: kke_texcook [--linear|--colour] [--force] [--threads N] PATH...\n");
        return 2;
    }
    int failures = 0;
    size_t files = 0, before = 0, after = 0;
    for (const fs::path& p : paths) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file(ec) && isImage(it->path())) {
                    failures += cookOne(it->path(), o, before, after);
                    ++files;
                }
            }
        } else if (fs::is_regular_file(p, ec) && isImage(p)) {
            failures += cookOne(p, o, before, after);
            ++files;
        } else {
            std::fprintf(stderr, "kke_texcook: %s is not an image or a folder\n", p.string().c_str());
            ++failures;
        }
    }
    if (before) std::printf("%zu image(s): GPU memory %.1f MB -> %.1f MB\n", files, before / 1048576.0, after / 1048576.0);
    else std::printf("%zu image(s), all up to date\n", files);
    return failures ? 1 : 0;
}
