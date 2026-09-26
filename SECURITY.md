# Security policy

## Supported versions

KKE is in early development (pre-alpha). Security fixes land on `main`
and in the next release; there are no maintained older branches yet.

## Reporting a vulnerability

Please **do not open a public issue** for a security problem.

Report it privately through GitHub: open the repository's **Security**
tab and choose **Report a vulnerability**
([direct link](https://github.com/Khyretos/kk-engine/security/advisories/new)).
Include what an attacker can do, the steps or a proof of concept to
reproduce it, and the commit you tested.

You'll get an answer within a week. Once a fix is out, the advisory is
published with credit to you unless you'd rather stay anonymous.

## What is in scope

The areas where untrusted input reaches the engine matter most:

- **Networking** (`engine/src/net/`): anything a remote peer sends,
  such as packets that crash, hang or take over a host or client.
- **Lua scripting** (`ScriptVM`): escaping the sandbox (file, process or
  network access from a script), or getting around the memory and
  instruction limits.
- **Game and asset loading**: `game.json` manifests, scene files, the
  marketplace index, models and other files a downloaded game can ship.
- **UI text** (RmlUi): markup injection through player-supplied text.

Bugs in third-party libraries should also be reported upstream; tell us
too if KKE ships the affected version.
