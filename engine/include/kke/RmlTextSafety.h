#pragma once

#include <string>

namespace kke {

// The one sanctioned way to place untrusted or user-generated text into
// RML markup that RmlUi will parse. Escapes the characters that would
// otherwise let the input be interpreted as markup — a new element, an
// attribute break-out, a style injection — rather than as plain text.
// Uses numeric character references (&#34; / &#39;) rather than the named
// &quot;/&apos; entities: verified empirically that RmlUi's XML parser
// renders &apos; as five literal characters instead of decoding it, so
// named entities beyond &amp;/&lt;/&gt; can't be assumed safe here.
//
// ALWAYS use this for: game.json manifest fields (title/description/tags)
// once a marketplace might import third-party game folders, eventual
// chat messages, or any other string whose author isn't this engine's
// own trusted code. This is the concrete implementation of the security
// rule the README's "Game folder convention & marketplace" section
// requires rather than just states.
std::string escapeRmlText(const std::string& untrustedText);

} // namespace kke
