bits 32
section .text.entry
[extern _kmain]
global _kernel_entry
_kernel_entry:
    call _kmain
    jmp $
