qemu-system-x86_64 \
    -drive file=./build/os.img,format=raw,if=ide \
    -accel tcg \
    -cpu qemu64 \
    -m 4G \
    -smp 1