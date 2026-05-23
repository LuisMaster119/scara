; ============================================================
; Generado por compilador SCARA — datos de traza en NASM x64
; Código ensamblador con funciones para acceso a datos.
; ============================================================

default rel

section .data

    global traza_len
    traza_len  dd  17

    global traza_x
    global traza_y
    global traza_z
    global traza_pinza
    global traza_vel

    traza_x        dd  0, 0, 40, 80, 120, 120, 120, 120, 120, 120, 120, 120, 163, 206, 250, 250, \
                    0
    traza_y        dd  0, 0, 50, 100, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, \
                    0
    traza_z        dd  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
                    0
    traza_pinza    dd  1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, \
                    1
    traza_vel      dd  100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, \
                    100

section .text

    global traza_get_len
    global traza_get_x
    global traza_get_y
    global traza_get_z
    global traza_get_pinza
    global traza_get_vel

; int traza_get_len(void)
traza_get_len:
    lea rax, [rel traza_len]
    mov eax, [rax]
    ret

; int traza_get_x(int idx)  [idx en rcx]
traza_get_x:
    lea rax, [rel traza_len]
    mov eax, [rax]
    cmp ecx, eax
    jge .ret_zero_x
    lea rax, [rel traza_x]
    movsxd rcx, ecx
    mov eax, [rax + rcx*4]
    ret
.ret_zero_x:
    xor eax, eax
    ret

; int traza_get_y(int idx)  [idx en rcx]
traza_get_y:
    lea rax, [rel traza_len]
    mov eax, [rax]
    cmp ecx, eax
    jge .ret_zero_y
    lea rax, [rel traza_y]
    movsxd rcx, ecx
    mov eax, [rax + rcx*4]
    ret
.ret_zero_y:
    xor eax, eax
    ret

; int traza_get_z(int idx)  [idx en rcx]
traza_get_z:
    lea rax, [rel traza_len]
    mov eax, [rax]
    cmp ecx, eax
    jge .ret_zero_z
    lea rax, [rel traza_z]
    movsxd rcx, ecx
    mov eax, [rax + rcx*4]
    ret
.ret_zero_z:
    xor eax, eax
    ret

; int traza_get_pinza(int idx)  [idx en rcx]
traza_get_pinza:
    lea rax, [rel traza_len]
    mov eax, [rax]
    cmp ecx, eax
    jge .ret_zero_p
    lea rax, [rel traza_pinza]
    movsxd rcx, ecx
    mov eax, [rax + rcx*4]
    ret
.ret_zero_p:
    xor eax, eax
    ret

; int traza_get_vel(int idx)  [idx en rcx]
traza_get_vel:
    lea rax, [rel traza_len]
    mov eax, [rax]
    cmp ecx, eax
    jge .ret_zero_v
    lea rax, [rel traza_vel]
    movsxd rcx, ecx
    mov eax, [rax + rcx*4]
    ret
.ret_zero_v:
    xor eax, eax
    ret

