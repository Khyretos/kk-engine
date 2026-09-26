# Devlog

Short, regular notes on what changed in KKE and why. The full record of
how each system was built is the [development history](../HISTORY.md); the
plan is in [Going to market](../GO_TO_MARKET.md).

## September 2026: the engine goes public

- **Public on GitHub under the MIT licence** (2026-09-03), with a Ko-fi
  page for anyone who wants to support it.
- **Lua scripting** with Garry's Mod-style hooks and timers, hot reload
  and a sandbox, and a first game written only in Lua (break the targets).
- **Movement** built on PointDown's principles: vaulting, climbing, ledge
  hang and shimmy, corners and jumping off.
- **Level saving** in the sandbox, **networking** (host, join, LAN
  discovery), **audio** with synthesized impact sounds and a sound
  visualizer.
- **This site**, the **starter template** and the first
  [tutorials](../tutorials/index.md).

Next: downloadable builds, benchmarks on real hardware, and the
v0.1.0-alpha launch.
