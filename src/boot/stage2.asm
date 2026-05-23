org 0x8000
bits 16
start2:
    cli
    mov ax, 0xB800
    mov es, ax
    mov byte [es:6], 'S'
    mov byte [es:7], 0x07

    E820_BUF equ 0x5000
    E820_COUNT equ 0x4ff0

    xor ax, ax
    mov ds, ax
    mov es, ax

    mov di, E820_BUF
    mov dword [E820_COUNT], 0
    xor ebx, ebx

    .e820_loop:
        mov eax, 0xE820
        mov edx, 0x534D4150
        mov ecx, 24
        int 0x15
        jc .e820_done
        cmp eax, 0x534D4150
        jne .e820_done
        add di, 24
        inc word [E820_COUNT]
        test ebx, ebx
        jnz .e820_loop
    .e820_done:

    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE32_SEG:init_pm
gdt_start:
    dq 0
gdt_code32:
    dq 0x00CF9A000000FFFF
gdt_data32:
    dq 0x00CF92000000FFFF
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
CODE32_SEG equ gdt_code32 - gdt_start
DATA32_SEG equ gdt_data32 - gdt_start
bits 32
init_pm:
    mov ax, DATA32_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov esp, 0x200000
    mov byte [0xB8008], 'P'
    mov byte [0xB8009], 0x07
    mov edi, 0xB8000
    mov ecx, 2000
    mov ax, 0x0720
.clear:
    mov word [edi], ax
    add edi, 2
    loop .clear
    mov eax, 0x8200
    jmp eax
times 512 - ($ - $$) db 0
