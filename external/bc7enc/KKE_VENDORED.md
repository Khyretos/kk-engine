# bc7enc, vendored

`bc7enc.c`, `bc7enc.h`, `bc7decomp.cpp`, `bc7decomp.h` and `LICENSE` are
copied unchanged from <https://github.com/richgel999/bc7enc> (commit
f66c2e489b07138f2673a2fb3d27c1aa1d565c48), by Richard Geldreich, Jr.,
under the MIT licence or public domain (your choice; see `LICENSE`).

The engine uses the BC7 encoder in `kke_texcook` (compressing textures
into KTX2) and the decoder as the fallback on GPUs without BC formats
(`engine/src/TextureCompress.cpp`). Four files, so they live here
instead of being fetched: the CI runners fetch every other dependency
through a Forgejo pull mirror.

To update: copy the files from a newer commit, change the commit above,
and run `kke_tests --gtest_filter='TextureCompress*:Ktx2*'`.
