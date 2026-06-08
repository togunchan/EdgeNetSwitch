# Docker Development Environment

This document describes the Linux development workflow used for EdgeNetSwitch
runtime work introduced during v1.9.3.

The macOS host remains a practical editing environment, but Linux-specific
runtime code is built and validated inside an Ubuntu container.

---

## 1. Motivation

EdgeNetSwitch is developed on macOS, but the runtime architecture is moving
toward Linux-specific primitives. The v1.9.3 work introduced `EventFd`, a
Linux-specific wakeup primitive that serves as a foundation for upcoming
`epoll`-based runtime infrastructure.

The following components require a Linux build environment:

- `eventfd`
- `epoll`
- future netlink integration
- future Yocto and QEMU runtime validation

These APIs and runtime surfaces are part of the Linux platform contract. They
are not available through the macOS SDK.

---

## 2. Docker Installation

Install Docker Desktop for macOS from Docker's official distribution.

After installation, verify the local Docker client and daemon:

```bash
docker --version
docker ps
```

`docker ps` should complete without a daemon connection error.

---

## 3. Starting the Development Environment

From the repository root:

```bash
docker run -it --rm \
    -v $(pwd):/workspace \
    ubuntu:24.04
```

This starts an interactive Ubuntu 24.04 shell with the repository mounted at
`/workspace`.

The flags are used as follows:

- `-it` starts an interactive terminal session.
- `--rm` removes the container after the shell exits.
- `-v $(pwd):/workspace` mounts the current repository into the container.

The source tree remains on the macOS host. Build artifacts created under the
mounted repository are visible from both macOS and the container.

---

## 4. Installing Build Dependencies

Inside the container:

```bash
apt update

apt install -y \
    build-essential \
    cmake \
    git
```

These packages provide the compiler, standard build tools, CMake, and Git
support required by the current EdgeNetSwitch build.

---

## 5. Building EdgeNetSwitch

Inside the container:

```bash
cd /workspace

mkdir -p build
cd build

cmake ..
cmake --build .
```

Daily development should preserve the build directory so CMake and the compiler
can reuse incremental build state.

Deleting the build directory is only required for:

- first-time Linux builds
- corrupted CMake cache
- path migration issues between host and container

This configures and builds EdgeNetSwitch using the Ubuntu toolchain. Linux-only
sources such as `src/system/event_source/EventFd.cpp` are compiled in this environment.

---

## 6. Running Tests

From the build directory:

```bash
ctest --output-on-failure
```

Test failures should be investigated inside the same Ubuntu environment used
for the build.

---

## 7. Daily Workflow

The normal development loop is:

- Edit code in VSCode on macOS.
- Build inside the Ubuntu container.
- Run tests inside the Ubuntu container.
- Exit the container when finished.

---

## 8. Known Limitation

VSCode clangd running on macOS cannot resolve Linux-only headers such as:

- `sys/eventfd.h`
- `sys/epoll.h`

This is expected when the editor uses the macOS SDK. Linux build and test
results are considered the source of truth for Linux-specific runtime components.

---

## 9. Future Evolution

The Docker workflow is intentionally minimal at this stage. As the runtime
surface grows, the environment can evolve toward:

- VSCode Dev Containers
- a dedicated project Dockerfile
- a Linux-native development workflow
- QEMU integration
- Yocto integration

A dedicated Docker image would eliminate repeated package installation and
standardize the compiler, CMake, and runtime tooling used by contributors.

Those additions should be introduced when they carry concrete build,
debugging, or runtime validation value for EdgeNetSwitch.
