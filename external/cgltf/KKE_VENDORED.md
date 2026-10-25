# cgltf, vendored

`cgltf.h` and `LICENSE` are copied unchanged from
<https://github.com/jkuhlmann/cgltf> tag `v1.15`
(commit 360db1a95480fe102ae9c69b27c5d101167ff5ba), MIT licence.

It is one header, so it lives here instead of being fetched: the CI
runners fetch every other dependency through a Forgejo pull mirror, and
one more mirror for a single file was not worth it. `engine/src/GltfAsset.cpp`
is the only file that compiles its implementation (`CGLTF_IMPLEMENTATION`).

To update: copy both files from a newer tag, change the tag and commit
above, and run `kke_tests --gtest_filter='Gltf*'`.
