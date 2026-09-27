// kke_cook: cooks art a game may ship but not hand out as source files
// (Synty packs) into files only this checkout's builds can read
// (kke/CookedFile.h, docs/COOKED_ART.md).
//
//   kke_cook --root DIR --out DIR --trace FILE   the files a trace lists (KKE_ASSET_TRACE)
//   kke_cook --root DIR --out DIR --all          every model, texture and .sk file under DIR
//   kke_cook --check DIR                         fail if DIR holds any art that isn't cooked
//
// Files keep their path relative to --root, so a game finds them by name
// as before: cook into <bin>/assets/synty. --root may be given more than
// once (a trace can span several pack folders). Exit code 0 = done.

#include "kke/CookedFile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::set<std::string> kArt = { ".fbx", ".obj", ".gltf", ".glb", ".png", ".tga", ".jpg", ".jpeg", ".bmp", ".psd", ".sk" };

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isArt(const fs::path& p) { return kArt.count(lower(p.extension().string())) != 0; }

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

int usage() {
    std::fprintf(stderr, "usage:\n  kke_cook --root DIR [--root DIR...] --out DIR (--trace FILE | --all)\n  kke_cook --check DIR\n");
    return 2;
}

int check(const fs::path& dir) {
    std::error_code ec;
    int plain = 0, cookedCount = 0;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::follow_directory_symlink, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec || !it->is_regular_file(ec) || !isArt(it->path())) continue;
        if (kke::cooked::isCookedFile(it->path().string())) {
            ++cookedCount;
        } else {
            ++plain;
            std::printf("not cooked: %s\n", it->path().string().c_str());
        }
    }
    std::printf("%d cooked, %d not cooked\n", cookedCount, plain);
    return plain == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> roots;
    fs::path out, traceFile, checkDir;
    bool all = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--root") roots.emplace_back(next());
        else if (a == "--out") out = next();
        else if (a == "--trace") traceFile = next();
        else if (a == "--all") all = true;
        else if (a == "--check") checkDir = next();
        else return usage();
    }
    if (!checkDir.empty()) return check(checkDir);
    if (roots.empty() || out.empty() || (traceFile.empty() == !all)) return usage();

    std::error_code ec;
    for (fs::path& r : roots) r = fs::weakly_canonical(fs::absolute(r, ec), ec); // games see packs through symlinks resolved
    // What to cook: (root, path) pairs.
    std::vector<std::pair<fs::path, fs::path>> files;
    std::set<fs::path> seen;
    auto add = [&](const fs::path& p) {
        const fs::path abs = fs::weakly_canonical(fs::absolute(p, ec), ec);
        for (const fs::path& r : roots) {
            const fs::path rel = abs.lexically_relative(r);
            if (rel.empty() || *rel.begin() == "..") continue;
            if (seen.insert(abs).second) files.emplace_back(r, rel);
            return;
        }
    };
    if (all) {
        for (const fs::path& r : roots)
            for (auto it = fs::recursive_directory_iterator(r, fs::directory_options::follow_directory_symlink, ec); it != fs::recursive_directory_iterator();
                 it.increment(ec))
                if (!ec && it->is_regular_file(ec) && isArt(it->path())) add(it->path());
    } else {
        std::ifstream in(traceFile);
        if (!in) {
            std::fprintf(stderr, "kke_cook: can't read %s\n", traceFile.string().c_str());
            return 1;
        }
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) add(fs::path(line));
    }
    if (files.empty()) {
        std::fprintf(stderr, "kke_cook: nothing to cook (no file lies under a --root)\n");
        return 1;
    }

    uint64_t bytesIn = 0;
    int failed = 0;
    for (const auto& [root, rel] : files) {
        std::vector<uint8_t> plain, cookedBytes;
        std::string error;
        if (!readFile(root / rel, plain)) {
            std::fprintf(stderr, "kke_cook: can't read %s\n", (root / rel).string().c_str());
            ++failed;
            continue;
        }
        if (kke::cooked::isCooked(plain)) cookedBytes = plain; // already cooked: copy as is
        else if (!kke::cooked::cook(plain, cookedBytes, &error)) {
            std::fprintf(stderr, "kke_cook: %s\n", error.c_str());
            return 1;
        }
        const fs::path dest = out / rel;
        fs::create_directories(dest.parent_path(), ec);
        std::ofstream o(dest, std::ios::binary | std::ios::trunc);
        if (!o.write(reinterpret_cast<const char*>(cookedBytes.data()), static_cast<std::streamsize>(cookedBytes.size()))) {
            std::fprintf(stderr, "kke_cook: can't write %s\n", dest.string().c_str());
            ++failed;
            continue;
        }
        bytesIn += plain.size();
    }
    std::printf("kke_cook: %zu file(s), %.1f MB, cooked into %s%s\n", files.size() - static_cast<size_t>(failed), bytesIn / (1024.0 * 1024.0),
                out.string().c_str(), failed ? " (some failed, see above)" : "");
    return failed ? 1 : 0;
}
