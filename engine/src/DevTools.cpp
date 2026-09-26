#include "kke/DevTools.h"

#include <cstdlib>

namespace kke::dev {

const char* env(const char* name) {
    if constexpr (kEnabled) {
        return name ? std::getenv(name) : nullptr;
    } else {
        (void)name;
        return nullptr;
    }
}

bool flag(const char* name) {
    const char* v = env(name);
    return v && *v && !(v[0] == '0' && v[1] == 0);
}

} // namespace kke::dev
