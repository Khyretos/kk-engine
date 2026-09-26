#pragma once

#include "kke/RigidWorld.h"
#include "kke/net/Visibility.h"

namespace kke::net {

// Line of sight through a host's level for Visibility (fog of war): only
// static level geometry blocks sight; crates, doors that move and players
// don't hide anyone (they're not reliable cover, and a crate knocked
// aside must not leave someone invisible). `world` must outlive the
// returned function.
Visibility::RayClear levelSight(const RigidWorld& world);

} // namespace kke::net
