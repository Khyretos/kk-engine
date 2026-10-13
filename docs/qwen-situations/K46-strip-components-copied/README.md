# Lesson K46: a step copied from the context file broke the dictated path

Situation: macOS job `mac-docker` (2026-10-13): docker/macos.Dockerfile
extracts libclang_rt.osx.a from LLVM's macOS tarball. The prompt gave the
member path in full and said to move "the file" to the clang resource dir;
docker/ci.Dockerfile was the context, and its Node step uses
`tar ... --strip-components=1`.

Wrong output: the Coder added `--strip-components=3` (copied from the
Node step's pattern), so the file landed in /tmp/18/lib/darwin, and `mv
/tmp/libclang_rt.osx.a` failed. The repair saw only the RUN echo (K47),
guessed "the ARM64 archive has another layout" and shortened the member
path, which then matched nothing (wrong-output.gz).

Corrected: Claude dropped the strip and moved the file from its full path.

Rule: when a context file has a similar step, say which of its options NOT
to copy ("no --strip-components"), or dictate the whole RUN line.

Rerun: pass = mac-docker builds the image on run 1.
