# 7OS

A small educational/example operating system written in C++ and x86 assembly.

## Overview

This repository contains a bootloader and a kernel (i386) used to produce a bootable disk image that can be run in QEMU.

Top-level structure:

- `src/boot/` - bootloader sources (assembly)
- `src/kernel/` - kernel sources in C++ and assembly
- `src/os/` - higher-level OS code
- `build/` - build artifacts (ignored)

## Prerequisites

Install the following tools:

- `nasm`
- `g++` (with `-m32` support / multilib toolchain for i386)
- `ld` and `objcopy` (binutils)
- `qemu-system-x86_64`

On Debian/Ubuntu you can install them with:

```bash
sudo apt update
sudo apt install build-essential gcc-multilib g++-multilib nasm binutils qemu-system-x86
```

## Build and run

To build the project and run it in QEMU, run the main script:

```bash
./build_boot.sh
```

The script produces binaries under `build/` and starts QEMU with the image `build/os.img`.

To run QEMU against an existing image without rebuilding:

```bash
./run_boot.sh
```

## Contributing

Feel free to open issues or pull requests on the remote repository.

## License

This project is covered by the `Custom Non-Commercial Source License 1.0`.
See the full text in [LICENSE](LICENSE).