#include "kke/RmlTextSafety.h"

namespace kke {

std::string escapeRmlText(const std::string& untrustedText) {
    std::string out;
    out.reserve(untrustedText.size());
    for (char c : untrustedText) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            // Numeric character references, not the named &quot;/&apos; —
            // verified empirically that RmlUi's XML parser renders &apos;
            // as the literal five characters instead of decoding it to a
            // quote mark, so named entities beyond &amp;/&lt;/&gt; aren't
            // safe to assume here. Numeric refs are the more universally
            // supported form and were verified to render correctly.
            case '"': out += "&#34;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

} // namespace kke
