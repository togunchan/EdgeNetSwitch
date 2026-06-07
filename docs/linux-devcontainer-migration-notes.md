# Linux Dev Container Migration Notes

These are working notes from the EdgeNetSwitch v1.9.3 runtime work.

This is not a formal setup guide. It is closer to the engineering diary I wish
I had while moving the project from "mostly macOS editing with occasional Linux
awareness" into a real Linux development environment.

The short version: `EventFd` made the project cross a line. Once runtime code
started depending on Linux-only APIs, the build environment and the editor
environment had to agree about what platform they were targeting.

---

## Why EventFd Changed Things

The v1.9.3 runtime work introduced `EventFd`, a small RAII wrapper around the
Linux `eventfd` primitive. It is intended to support deterministic wakeup and
shutdown behavior for the upcoming `epoll` runtime integration.

That immediately exposed a platform boundary:

- `eventfd` is Linux-specific.
- `<sys/eventfd.h>` does not exist in the macOS SDK.
- `EFD_NONBLOCK` does not exist on macOS.
- `EFD_CLOEXEC` does not exist on macOS.

The source was valid for Linux, but the macOS editor did not know what to do
with it. CMake builds and editor diagnostics started telling different stories.

That is a bad place to be. A runtime file can compile cleanly in the intended
environment while the editor paints it red. After a while, that trains me to
ignore diagnostics, which is exactly when diagnostics become dangerous.

---

## Initial Assumptions

At first I did not treat this as an environment problem.

I suspected the compiler. Maybe Apple Clang was missing something. Maybe GNU
extensions were involved. Maybe the Linux code needed a different include or a
feature macro.

Then I suspected CMake. Maybe `EventFd.cpp` was not wired into the right target.
Maybe the include directories were attached to one target but not another.
Maybe the target source list was right for the build but invisible to clangd.

Then I suspected include paths. The first editor error was about a missing
header, and missing headers usually point to a bad `-I` path.

All of those were reasonable suspicions, but they were not the root issue. The
real issue was simpler: I was asking macOS tooling to understand Linux runtime
code.

---

## Bringing In Docker

The first step was to get a Linux build environment that was easy to repeat.

I installed Docker Desktop and verified that the local Docker client and daemon
worked:

```bash
docker --version
docker ps
```

Then I launched an Ubuntu 24.04 container and mounted the repository into it:

```bash
docker run -it --rm \
    -v $(pwd):/workspace \
    ubuntu:24.04
```

Inside the container, the project lived at `/workspace`. That made the
repository visible from both macOS and Linux: I could edit on the host and build
inside Ubuntu.

This was the first point where the direction became obvious. Docker was not
just a deployment tool here. It was the cleanest way to make the development
environment match the runtime environment.

---

## First Linux Build

The first Linux build did not succeed immediately.

The mistake was reusing the old `build` directory. That directory had been
configured on macOS, so `CMakeCache.txt` still described a macOS build tree.
When I tried to use it from Ubuntu, CMake correctly complained that the cache no
longer matched the source and build paths.

The fix was not clever:

```bash
rm -rf build
mkdir -p build
cd build
cmake ..
cmake --build .
```

After regenerating the project inside Linux, the build succeeded.

That failure was useful. It made the boundary concrete: the `build` directory is
not just a pile of object files. It carries configured compiler paths, source
paths, generator state, and platform assumptions.

---

## EventFd Integration

`EventFd.cpp` should not be compiled on macOS. The implementation includes
Linux-only headers and calls Linux-only APIs, so pretending it is portable would
only make the project harder to reason about.

The daemon target keeps the source Linux-gated:

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(EdgeNetSwitchDaemon
        PRIVATE
            src/system/event_source/EventFd.cpp
    )
endif()
```

That is the right ownership boundary for now. The abstraction can have a stable
project-level name, but the implementation belongs to the Linux runtime path.

Linux-only runtime code should stay Linux-only. The validation should happen on
Linux, not through a set of host-side compromises that make the editor quiet
while weakening the actual platform contract.

---

## Moving To A Dev Container

The raw Docker workflow fixed the build, but it did not fully fix the editing
loop.

The next step was moving EdgeNetSwitch into a VSCode Dev Container based on
Ubuntu 24.04. That gave the editor the same environment as the build:

- Linux filesystem paths
- Linux headers
- GCC and libstdc++
- Ubuntu `clangd`
- CMake-generated `compile_commands.json`

That should have been enough.

But VSCode still showed red diagnostics.

The Linux build passed. `EventFd.cpp` compiled. `<sys/eventfd.h>` existed.
`eventfd`, `EFD_NONBLOCK`, and `EFD_CLOEXEC` were all available to the compiler.

The editor still complained.

---

## The clangd Investigation

The next failure looked stranger:

- `<mutex>` was not found.
- `std::` symbols were not found.
- `ssize_t` was unknown.
- `errno` was unknown.
- `eventfd` symbols were unresolved.

That was too broad to be an `EventFd.cpp` problem. If `<mutex>` is missing in an
Ubuntu container with GCC installed, clangd is not using the same toolchain view
as the build.

The culprit was `.clangd`.

It still contained macOS-specific flags:

```yaml
CompileFlags:
  CompilationDatabase: build
  Add:
    - -Iinclude
    - -std=c++20
    - --target=arm64-apple-macosx
    - -isysroot
    - /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
```

So Ubuntu clangd was running inside the container, but the project configuration
was forcing it to parse as if it were targeting macOS with the Xcode SDK.

That explained everything. Linux clangd was not broken. It was doing what the
project configuration told it to do, and the project configuration was stale.

---

## Fixing clangd

The fix was to stop overriding the toolchain in `.clangd`.

The correct project-level configuration for the Dev Container workflow is:

```yaml
CompileFlags:
  CompilationDatabase: build
```

That keeps clangd pointed at CMake's compilation database and lets CMake remain
the source of truth for:

- include paths
- source files
- language standard
- target-specific compile flags
- Linux-gated sources such as `EventFd.cpp`

I also regenerated `compile_commands.json` inside the Dev Container by
reconfiguring the build directory there. That matters because
`compile_commands.json` stores absolute paths. A database generated under
`/workspace` is not the same as one generated under
`/workspaces/EdgeNetSwitch`.

The checks were straightforward:

- `EventFd.cpp` has an entry in `build/compile_commands.json`.
- The entry includes the project include directory.
- The entry is generated from the Linux daemon target.
- The paths match the Dev Container workspace path.

At that point, clangd had the same basic view as the compiler.

---

## The Final Surprise

Almost all diagnostics disappeared after fixing `.clangd` and regenerating the
compilation database.

One include error remained:

```text
'edgenetswitch/system/event_source/EventFd.hpp' file not found
```

The compile command was correct. The include path was present. The file existed.
The CMake target wiring was correct.

The last problem was stale language-server state.

Restarting clangd, and in practice restarting VSCode, cleared the final error.
After that, `EventFd.cpp` parsed cleanly in the editor.

That was a useful reminder: clangd is a long-running process with caches and
indexes. When the workspace path, compile database, and toolchain configuration
all change underneath it, a restart is not superstition. It is part of the
migration.

In the end, the issue was never `EventFd` itself. The migration exposed several
layers of tooling assumptions: platform-specific APIs, build configuration,
compilation databases, editor integration, and language-server state. Resolving
those layers was necessary before continuing the runtime work.

---

## Current Status

The migration is now in a good state:

- Docker is working.
- The Ubuntu Dev Container is working.
- clangd is running inside the container.
- `compile_commands.json` is working.
- Linux headers resolve.
- `sys/eventfd.h` resolves.
- `EventFd.cpp` compiles.
- Editor diagnostics match the Linux runtime environment.
- Tests are passing.

EdgeNetSwitch is ready to continue the v1.9.3 Epoll Runtime Integration.
