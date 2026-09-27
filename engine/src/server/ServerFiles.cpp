#include "kke/server/ServerFiles.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace kke::server {

bool writeFileAtomic(const std::string& path, const std::string& text, std::string* error) {
    const std::filesystem::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        if (error) *error = path + ": can't make the folder: " + ec.message();
        return false;
    }
    std::filesystem::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = path + ": can't write (is the folder writable?)";
            return false;
        }
        out << text;
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            if (error) *error = path + ": write failed (disk full?)";
            return false;
        }
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        if (error) *error = path + ": can't replace the file: " + ec.message();
        return false;
    }
    return true;
}

bool readFile(const std::string& path, std::string& text, bool* exists) {
    std::error_code ec;
    const bool there = std::filesystem::exists(path, ec);
    if (exists) *exists = there;
    if (!there) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

} // namespace kke::server
