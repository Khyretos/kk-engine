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
commit it, and never commit `.kke-art.key` or `.kke-art.build` (all are
in `.gitignore`).

## What the script does

1. **Keys.** Makes `.kke-art.key` at the repo root once: the checkout's
   secret, 32 bytes from `/dev/urandom` as 64 hex digits. Then writes a
   new `.kke-art.build` (a 16-byte id) for this bake. The build's key is
   SHA3-256 over both, so every bake gets its own key and art from one
   bake doesn't load in another's builds. The Linux and Windows builds
   of one bake share the id, so they read the same cooked art.
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

Same file name as the original. Contents: `KKECOOK2` (8 bytes), the
bake's 8-byte build tag, a random 24-byte nonce, a 16-byte MAC, then the
file encrypted with XChaCha20-Poly1305 (Monocypher's
`crypto_aead_lock`, the build tag as authenticated data so a file can't
be relabelled). Every loader (models, textures, interior colours,
Sidekick `.sk`) reads files through `kke::cooked::readAssetFile`
(`kke/CookedFile.h`), which returns plain files unchanged and decrypts
cooked ones, so game code doesn't know the difference. A build from
another bake, another checkout or the public release refuses a cooked
file ("cooked for another build") and falls back to stand-ins, like a
missing pack.

## How far the protection goes

What it does, all with standard parts (SHA3 in CMake, Monocypher, no
extra libraries):

- **No raw art in the download.** Every model and texture is encrypted;
  the packager refuses a download holding a raw one.
- **The secret stays home.** `.kke-art.key` is only read at configure
  time on your PC. The executable holds the bake's derived key, never
  the secret, so one cracked download says nothing about the next.
- **The key isn't stored whole.** CMake writes it as two halves (the key
  XOR a mask, `build/engine/generated/kke_art_key.h`). They're joined in
  a local buffer only while a file is being decrypted and wiped
  (`crypto_wipe`) right after. The halves are read through `volatile` so
  the compiler can't join them at build time; a search of the built
  executables for the key finds nothing.
- **A key per bake.** A leaked key opens one bake's download, not the
  others.

What it deliberately doesn't do: no anti-debugger tricks, no
self-destruct, no checks that kill the game, nothing that runs outside
the game. Those break on real PCs, set off antivirus and only bother
honest players (the same reason as [ANTI_CHEAT.md](ANTI_CHEAT.md)).

What no protection can stop: to draw the art the game has to hand plain
meshes and textures to the GPU, and a graphics debugger such as RenderDoc
can capture them from there. That is true of every Unity, Unreal and AAA
game. The licence asks that you don't hand out the source files in a
form anyone can lift out, and this does that.

File names and the names inside the files (meshes, bones) are kept.
Names on disk are how the engine finds art (AssetCatalog, the Synty
folder search), and bone names are how animations are matched to
characters. Inside a cooked file they're encrypted with everything
else, so stripping them wouldn't make the art harder to get at.

## Tools

```bash
kke_cook --root PACKS_DIR [--root DIR2 ...] --out OUT_DIR --trace TRACE.txt   # cook the files a trace lists
kke_cook --root PACKS_DIR --out OUT_DIR --all                                 # cook every art file
kke_cook --check DIR                                                          # exit 1 if any art file in DIR is raw
KKE_ASSET_TRACE=trace.txt ./some_game                                         # record which art a game opens
```

Art extensions: `.fbx .obj .gltf .glb .png .tga .jpg .jpeg .bmp .psd .sk`.
