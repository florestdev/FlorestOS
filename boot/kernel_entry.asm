; Kernel entry - disable IRQs (no IDT yet), then call kmain

[BITS 32]
[EXTERN kmain]
[GLOBAL _start]

_start:
    cli                     ; CRITICAL: no IDT -> any IRQ = chaos/flicker

    ; Mask all PIC interrupts
    mov al, 0xFF
    out 0x21, al            ; master PIC
    out 0xA1, al            ; slave PIC

    mov esp, 0x90000
    cld

    call kmain

.hang:
    cli
    hlt
    jmp .hang
