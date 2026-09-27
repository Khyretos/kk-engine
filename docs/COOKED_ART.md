# Cooked art: sending friends a build with the Synty art

The public downloads never contain Synty art: the licence lets you ship
the art *inside a game*, but not as files anyone can lift out and reuse.
The engine normally reads the packs' raw FBX and PNG files, and shipping
those would be exactly that. So the demos run on stand-in blocks unless
the player owns the packs and sets `KKE_ASSETS_DIR`.

Games made with store-bought art solve this the same way: the art is
*cooked* into the game's own format at build time, so what ships is data
only that game reads. KKE does that with one step.

## Bake a private download (the short version)

On your own PC, with the packs extracted somewhere:

```bash
tools/packaging/bake_with_art.sh --assets /path/to/your/synty/packs
tools/packaging/bake_with_art.sh --assets /path/to/your/synty/packs --windows   # also the Windows zip (Docker)
```

It needs a screen: step 3 opens every demo for a few seconds. When it
finishes, the archives are in `dist/`, named `*-with-art*`. Send them to
friends directly (a chat, a private link).

**Never** upload a `-with-art` archive to the public GitHub release or
commit it, and never commit `.kke-art.key` (both are in `.gitignore`).

## What the script does

1. **Key.** Makes `.kke-art.key` (32 random bytes from `/dev/urandom`,
   as 64 hex digits) at the repo root once; any configure makes one if
   it's missing. Builds carry the key compiled in (`build/.../generated/kke_art_key.h`, only
   visible to `engine/src/CookedFile.cpp`). Keep the file: art cooked
   with one key only loads in builds made with the same key.
2. **Build.** A Release build with FEMFX in `build-art/`.
3. **Trace.** Runs `kke_benchmark --seconds 3` with `KKE_ASSETS_DIR` set
   and `KKE_ASSET_TRACE=<file>`, which makes the engine write down every
   art file it opens. That list is what the demos really use, so the
   download holds a few hundred files instead of every pack.
   `--all-art` skips this and cooks everything (much bigger).
4. **Cook.** `kke_cook` writes each traced file to `build-art/cooked-art/`
   under the same relative path and name, encrypted; the CC0 animations
   in `assets/animations/` go along too. `kke_cook --check` confirms
   nothing raw slipped through.
5. **Package.** `tools/packaging/package.sh --cooked build-art/cooked-art`
   puts the cooked folder in the download's `assets/`. The packager's
   art check refuses any raw model or texture; only cooked files pass.

## The cooked format

Same file name as the original. Contents: `KKECOOK1` (8 bytes), a random
24-byte nonce, a 16-byte MAC, then the file encrypted with
XChaCha20-Poly1305 (Monocypher's `crypto_aead_lock`). Every loader
(models, textures, interior colours, Sidekick `.sk`) reads files through
`kke::cooked::readAssetFile` (`kke/CookedFile.h`), which returns plain
files unchanged and decrypts cooked ones, so the game code doesn't know
the difference. A build with another key (anyone else's checkout, the
public release) reports a cooked file as unreadable and falls back to
stand-ins, like a missing pack.

This is not DRM against a determined attacker (the key is in the
executable); it makes the art data only this game reads, which is what
the licence asks of a shipped game.

## Tools

```bash
kke_cook --root PACKS_DIR [--root DIR2 ...] --out OUT_DIR --trace TRACE.txt   # cook the files a trace lists
kke_cook --root PACKS_DIR --out OUT_DIR --all                                 # cook every art file
kke_cook --check DIR                                                          # exit 1 if any art file in DIR is raw
KKE_ASSET_TRACE=trace.txt ./some_game                                         # record which art a game opens
```

Art extensions: `.fbx .obj .gltf .glb .png .tga .jpg .jpeg .bmp .psd .sk`.
