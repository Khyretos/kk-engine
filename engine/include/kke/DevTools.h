#pragma once

// Developer tools and shipping builds (docs/ANTI_CHEAT.md "Shipping builds").
//
// A game built with -DKKE_SHIPPING=ON compiles out what a player must not
// reach: the ImGui developer panels (F1), the Lua console, script hot
// reload, and the debug environment switches (virtual input devices,
// intro skipping, test scenes). Everything else behaves the same.
//
// Why compiled out rather than hidden behind a setting: a setting is a
// byte in a file or in memory, and flipping it is the first thing a cheat
// tool tries. Code that isn't in the binary can't be switched back on.
//
// Engine and game code ask `kke::dev::kEnabled` (a constant, so the
// compiler drops the disabled branch) and read debug switches through
// `kke::dev::env`, never std::getenv directly.

#ifndef KKE_DEV_TOOLS
#define KKE_DEV_TOOLS 1
#endif

namespace kke::dev {

inline constexpr bool kEnabled = KKE_DEV_TOOLS != 0;

// std::getenv for developer switches (KKE_SKIP_INTRO, KKE_VIRTUAL_INPUT,
// ...): the variable's value in developer builds, always nullptr in
// shipping builds. Settings a player may legitimately change (VRAM budget,
// thread counts) keep using std::getenv.
const char* env(const char* name);

// True when env(name) is set to something other than "" or "0".
bool flag(const char* name);

} // namespace kke::dev
