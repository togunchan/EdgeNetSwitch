# DevLog 01 — QEMU Installation & Validation
Date: 2025-12-07
Author: Murat Toğunçhan Düzgün

## Summary
QEMU will be installed and validated on macOS. 
This environment will serve as the virtual hardware backend for EdgeNetSwitch.

## Steps Taken
[Step 1 — QEMU Installation]
Command: brew install qemu
Status: Started installation of QEMU via Homebrew.

[Step 2 — Verify Installation]
Command: qemu-system-aarch64 --version
Output: QEMU emulator version 10.1.3
Status: Verified QEMU ARM64 emulator is working correctly.

[Exit Method Test]
Attempted to exit QEMU using Ctrl+A combinations, but macOS terminal captured 
the key bindings. Switched to an alternative method:

Command:
pkill qemu-system-aarch64

Result:
QEMU terminated gracefully with:
"terminating on signal 15"

Status: PASS

