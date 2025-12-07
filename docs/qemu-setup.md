# QEMU Setup Guide (macOS, ARM64)

This document describes how to install and validate QEMU for use with the
EdgeNetSwitch virtual embedded Linux platform. QEMU serves as the virtual
hardware backend where Yocto-built kernel images, root filesystems, and
kernel modules will be tested.

---

## 1. Install QEMU on macOS

QEMU can be installed using Homebrew:

```bash
brew install qemu
```

After installation, verify the ARM64 emulator:

```bash
qemu-system-aarch64 --version
```

A successful version output confirms that QEMU is correctly installed.

---

## 2. Minimal Test: Launching an Empty ARM Machine

Before integrating Yocto-generated kernel images, we test whether QEMU can
successfully instantiate a virtual ARM board.

Run the following command:

```bash
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a53 \
  -m 1024 \
  -nographic
```

### Expected Behavior

Since no kernel or initrd is provided, QEMU will:

* Initialize the virtual hardware
* Take control of the terminal
* Display **no output**
* Wait idle for instructions

This is the correct and expected behavior for an empty machine with no software
to boot.

### Exiting QEMU

If `Ctrl + A`, `X` does not work on macOS Terminal, terminate QEMU from another shell:

```bash
pkill qemu-system-aarch64
```

---

## 3. Initial Run Script (Deferred)

The run script originally shown here is intentionally **not included** in the
repository at this stage. The script will become relevant only after:

* The first Yocto image is successfully built
* Kernel (`Image`) and root filesystem (`rootfs.cpio.gz`) artifacts exist
* The C++ daemon and kernel modules are ready to be deployed inside the image

At that point, the script will be added to:

`tools/scripts/run-qemu.sh`

and will contain the appropriate boot commands referencing the newly generated
Yocto artifacts.

This ensures the repository remains clean and no placeholder or non-functional
files are committed prematurely.

---

## Summary

QEMU is installed and functioning as expected. The environment is now ready for:

* Building the first Yocto image
* Testing initial kernel modules
* Booting EdgeNetSwitch’s custom Linux system
* Running the C++ daemon inside a virtual ARM device

This completes the QEMU preparation stage.
