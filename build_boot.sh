#!/usr/bin/env bash
set -e

CC="g++"

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "Toolchain mancante: g++ non trovato in PATH."
    exit 1
fi

mkdir -p ./build

# ASM
nasm -f bin ./src/boot/stage2.asm -o ./build/stage2.bin
nasm -f elf32 ./src/kernel/arch/i386/interrupts.asm -o ./build/interrupts.o
nasm -f elf32 ./src/kernel/arch/i386/kernel_entry.asm -o ./build/kernel_entry.o

# CPP files
mapfile -t cppFiles < <(find ./src/kernel ./src/os -name "*.cpp" 2>/dev/null || true)

objFiles=(
    "./build/kernel_entry.o"
    "./build/interrupts.o"
)

for file in "${cppFiles[@]}"; do
    objName="$(basename "${file%.cpp}").o"
    objPath="./build/$objName"

    objFiles+=("$objPath")

    "$CC" "$file" \
        -o "$objPath" \
        -c \
        -ffreestanding \
        -m32 \
        -fno-pic \
        -fno-pie \
        -fno-stack-protector \
        -fno-asynchronous-unwind-tables \
        -fno-exceptions \
        -fno-rtti \
        -nostdlib \
        -I ./src/kernel \
        -I ./src/kernel/arch/i386 \
        -I ./src/kernel/drivers \
        -I ./src/kernel/util \
        -I ./src/kernel/mem \
        -I ./src/os
done

# Link
ld -m elf_i386 \
   --no-warn-rwx-segments \
   -T ./src/kernel/arch/i386/linker.ld \
   -o ./build/kernel.elf \
   "${objFiles[@]}"

# Binary
objcopy -O binary ./build/kernel.elf ./build/kernel.bin

# Sector math
kernelSize=$(stat -c%s ./build/kernel.bin)
kernelSectors=$(( (kernelSize + 511) / 512 ))
kernelBytesTarget=$(( kernelSectors * 512 ))

totalSectors=$(( 1 + kernelSectors ))

# Bootloader
nasm -f bin ./src/boot/boot.asm \
    -o ./build/boot.bin \
    -D TOTAL_SECTORS=$totalSectors

# Pad kernel
cp ./build/kernel.bin ./build/kernel_padded.bin

truncate -s "$kernelBytesTarget" ./build/kernel_padded.bin

# Build final OS image
cat \
    ./build/boot.bin \
    ./build/stage2.bin \
    ./build/kernel_padded.bin \
    > ./build/os.bin

# Create disk image
diskPath="./build/os.img"
diskSize=$((2 * 1024 * 1024 * 1024))

truncate -s "$diskSize" "$diskPath"

dd if=./build/os.bin of="$diskPath" conv=notrunc status=progress

# Run QEMU
qemu-system-x86_64 \
    -drive file=./build/os.img,format=raw,if=ide \
    -accel tcg \
    -cpu qemu64 \
    -m 4G \
    -smp 1
