; Custom MBR bootloader for VNL kernel
; Loads ELF64 kernel from disk, sets VBE graphics mode, and passes Multiboot1 structure to kernel.
org 0x7C00
bits 16

start:
    jmp 0x0000:init_cs

init_cs:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl        ; Save BIOS boot drive

    ; 1. Enable A20 gate
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 2. Query Memory size (E801)
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jnc .save_mem
    mov cx, 15 * 1024
    mov dx, 240 * 16
.save_mem:
    shl dx, 6                   ; DX * 64
    add cx, dx                  ; total memory upper in KB
    mov [mem_upper_kb], cx

    ; Load sector 2 (containing 32-bit entry) to 0x7E00
    mov ax, 0x0201              ; AH=02 (read sectors), AL=1 (number of sectors)
    mov cx, 0x0002              ; CH=0, CL=2 (sector 2)
    mov dh, 0                   ; Head 0
    mov dl, [boot_drive]
    mov bx, 0x7E00              ; ES:BX = 0x0000:0x7E00
    int 0x13
    jc .disk_error

    ; 3. Query VBE Controller Info to get the list of supported modes
    mov ax, 0x4F00
    mov di, 0x8000              ; Controller Info block at 0x8000
    int 0x10
    cmp ax, 0x004F
    jne .vbe_error

    ; Get pointer to mode list (offset 14 is offset, 16 is segment)
    mov si, [0x8000 + 14]
    mov ax, [0x8000 + 16]
    mov gs, ax                  ; GS:SI points to the mode list

.mode_search_loop:
    mov cx, [gs:si]
    cmp cx, 0xFFFF
    je .no_mode_found

    ; Query info for this mode
    push si
    mov ax, 0x4F01
    mov di, 0x8200              ; Temporary ModeInfoBlock at 0x8200
    int 0x10
    pop si

    cmp ax, 0x004F
    jne .next_mode

    ; Check if mode matches 1024x768x32
    cmp word [0x8200 + 18], 1024
    jne .next_mode
    cmp word [0x8200 + 20], 768
    jne .next_mode
    cmp byte [0x8200 + 25], 32
    jne .next_mode

    ; Check if LFB is supported (bit 7 of attributes at offset 0 must be 1)
    mov ax, [0x8200 + 0]
    test ax, 0x0080
    jz .next_mode

    ; Match found! Save mode number
    mov [vbe_mode_number], cx

    ; Copy 256-byte ModeInfoBlock from 0x8200 to 0x8000
    mov si, 0x8200
    mov di, 0x8000
    mov cx, 128                  ; 128 words = 256 bytes
    rep movsw
    jmp .mode_found

.next_mode:
    add si, 2                   ; Next mode in list
    jmp .mode_search_loop

.no_mode_found:
    jmp .vbe_error

.mode_found:
    ; Set the VBE Mode (mode | 0x4000 for LFB)
    mov ax, 0x4F02
    mov bx, [vbe_mode_number]
    or bx, 0x4000               ; Enable Linear Framebuffer
    int 0x10
    cmp ax, 0x004F
    jne .vbe_error

    ; Populate Multiboot1 structure at 0x9000
    mov di, 0x9000
    xor ax, ax
    mov cx, 64
    rep stosw

    mov dword [0x9000 + 0], 0x1001  ; flags: mem + framebuffer
    mov dword [0x9000 + 4], 640     ; mem_lower

    movzx eax, word [mem_upper_kb]
    mov [0x9000 + 8], eax           ; mem_upper

    mov eax, [0x8000 + 40]
    mov [0x9000 + 88], eax          ; framebuffer_addr (32-bit)

    movzx eax, word [0x8000 + 16]
    mov [0x9000 + 96], eax          ; framebuffer_pitch

    movzx eax, word [0x8000 + 18]
    mov [0x9000 + 100], eax         ; framebuffer_width

    movzx eax, word [0x8000 + 20]
    mov [0x9000 + 104], eax         ; framebuffer_height

    mov al, [0x8000 + 25]
    mov [0x9000 + 108], al          ; framebuffer_bpp

    mov byte [0x9000 + 109], 1      ; framebuffer_type = RGB

    ; 5. Load kernel from disk in chunks of 64 sectors to avoid segment boundary overflow
    mov cx, 17                  ; 17 chunks * 32 KB = 544 KB
    mov word [dap_sectors], 64
    mov word [dap_offset], 0
    mov word [dap_segment], 0x1000 ; starts at 0x10000
    mov dword [dap_lba_low], 2     ; starts at LBA 2 (kernel is after the 2-sector bootloader)
    mov dword [dap_lba_high], 0

.read_loop:
    push cx
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc .disk_error

    add word [dap_segment], 0x800  ; segment += 32KB
    add dword [dap_lba_low], 64    ; LBA += 64
    pop cx
    loop .read_loop

    ; 6. Transition to Protected Mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1                   ; Set PE bit
    mov cr0, eax
    jmp 0x08:pm_start           ; Far jump to 32-bit segment

.vbe_error:
    mov al, 'V'
    jmp .error
.disk_error:
    mov al, 'D'
.error:
    mov dx, 0x3F8
    out dx, al
.halt:
    hlt
    jmp .halt

; BIOS Data
boot_drive:      db 0
mem_upper_kb:    dw 0
vbe_mode_number: dw 0

align 4
dap:
    db 0x10                     ; Packet size
    db 0                        ; Reserved
dap_sectors:
    dw 64                       ; Number of sectors
dap_offset:
    dw 0x0000                   ; Offset
dap_segment:
    dw 0x1000                   ; Segment
dap_lba_low:
    dd 2                        ; LBA low
dap_lba_high:
    dd 0                        ; LBA high

; GDT for Protected Mode
align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF
    dw 0
    db 0
    db 0x9A                     ; Access: Present, Ring 0, Executable, Read/Write
    db 0xCF                     ; Flags: Page granular, 32-bit
    db 0
gdt_data:
    dw 0xFFFF
    dw 0
    db 0
    db 0x92                     ; Access: Present, Ring 0, Writable Data
    db 0xCF                     ; Flags: Page granular, 32-bit
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Fill up to 510 bytes (Sector 0 signature)
times 510-($-$$) db 0
dw 0xAA55

; ======================================================================
; Sector 1 (32-bit Protected Mode) - Loaded at 0x7E00
; ======================================================================
bits 32
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x7C00             ; Stack below bootloader

    ; Parse ELF64 header of kernel loaded at 0x10000
    cmp dword [0x10000], 0x464C457F ; check ELF magic
    jne .elf_error

    mov ebx, [0x10000 + 32]     ; PHT offset
    movzx ecx, word [0x10000 + 56] ; Number of Phdrs
    movzx edx, word [0x10000 + 54] ; Size of Phdr entry

.ph_loop:
    push ecx
    mov eax, [0x10000 + ebx + 0] ; p_type
    cmp eax, 1                  ; PT_LOAD
    jne .next_ph

    ; Load segment
    mov edi, [0x10000 + ebx + 24] ; p_paddr (dest phys address)
    mov esi, [0x10000 + ebx + 8]  ; p_offset (offset in file)
    add esi, 0x10000            ; source address
    mov ecx, [0x10000 + ebx + 32] ; p_filesz
    push edi
    push ecx
    rep movsb                   ; Copy segment data
    pop ecx
    pop edi

    ; Zero BSS: memsz - filesz
    mov eax, [0x10000 + ebx + 40] ; p_memsz
    sub eax, ecx
    jz .next_ph
    add edi, ecx                ; dest + filesz
    mov ecx, eax
    xor al, al
    rep stosb                   ; Zero BSS

.next_ph:
    pop ecx
    add ebx, edx
    dec ecx
    jnz .ph_loop

    ; Jump to entry point
    mov eax, [0x10000 + 24]     ; entry address
    mov ebx, 0x9000             ; Multiboot info structure
    mov ecx, 0x2BADB002         ; Multiboot magic
    push eax
    mov eax, ecx
    ret

.elf_error:
    mov al, 'E'
    mov dx, 0x3F8
    out dx, al
.halt32:
    hlt
    jmp .halt32

; Pad Sector 1 to exactly 512 bytes (total 1024 bytes)
times 1024-($-$$) db 0
