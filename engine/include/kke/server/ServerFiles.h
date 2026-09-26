#pragma once

#include <string>

namespace kke::server {

// The files a server keeps (access list, leaderboards) are replaced
// whole: written next to the target, then renamed over it, so a crash or
// a full disk leaves the old file rather than half a new one. false and
// `error` when it couldn't; the old file is untouched then.
bool writeFileAtomic(const std::string& path, const std::string& text, std::string* error = nullptr);
// true and `text` when the file is there and readable; false otherwise
// (`exists` says which: a missing file is usually fine, an unreadable one isn't).
bool readFile(const std::string& path, std::string& text, bool* exists = nullptr);

} // namespace kke::server
