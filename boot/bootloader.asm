; FlorestOS-1.0 bootloader — VirtualBox IDE HDD + ISO
[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    sti

    mov si, msg
    call print

    mov dl, [boot_drive]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc try_chs
    cmp bx, 0xAA55
    jne try_chs
    test cl, 1
    jz try_chs

    xor bp, bp
.lba:
    mov ax, bp
    shl ax, 5
    add ax, 0x1000
    mov [dap_seg], ax
    mov word [dap_off], 0
    mov word [dap_count], 1
    mov ax, bp
    inc ax
    mov word [dap_lba], ax
    mov word [dap_lba+2], 0
    mov word [dap_lba+4], 0
    mov word [dap_lba+6], 0
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc try_chs
    inc bp
    cmp bp, 256
    jb .lba
    jmp ok

try_chs:
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 128
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc fail

ok:
    call a20
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm

fail:
    mov si, err
    call print
    jmp $

print:
    lodsb
    or al, al
    jz .r
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp print
.r: ret

a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

[BITS 32]
pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    cli
    jmp 0x10000

dap:
    db 16, 0
dap_count: dw 1
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

boot_drive: db 0

gdt:
    dq 0
gdt_code:
    dw 0xFFFF
    dw 0
    db 0
    db 10011010b
    db 11001111b
    db 0
gdt_data:
    dw 0xFFFF
    dw 0
    db 0
    db 10010010b
    db 11001111b
    db 0
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

msg: db "FlorestOS boot", 13, 10, 0
err: db "DISK ERROR", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
